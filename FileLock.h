/**
 * filelock-task -- A statefultask-based file lock task.
 *
 * @file
 * @brief Declaration of FileLock.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of filelock-task.
 *
 * Filelock-task is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Filelock-task is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with filelock-task.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "statefultask/AIStatefulTaskMutex.h"
#include "utils/AIAlert.h"
#include "debug.h"
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/intrusive_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <set>

#pragma once

class AIStatefulTask;
class FileLock;
class FileLockAccess;

// Helper class for FileLock.
//
// An object of this type contains the "canonical" path (that is, the *first* path used for a
// FileLock object, passed through std::filesystem::absolute(filename).lexically_normal(),
// of all (subsequent) 'std::filesystem::equivalent' paths -- not the result of
// std::filesystem::canonical which also removes all symbolic links), the threadsafe
// boost::interprocess::file_lock instance with a reference count of the number of FileLockAccess
// objects pointing to this instance, and an instance of AIStatefulTaskLockSingleton: a reference
// counted pointer to the AIStatefulTask that owns the lock, if any.
//
// There is only one instance per canonical path (read: inode) of this class.
// The user can not create a FileLockSingleton; instead use class FileLock and FileLockAccess.
//
class FileLockSingleton : public AIStatefulTaskMutex
{
  friend class FileLock;
  friend class FileLockAccess;

 private:
  struct Data
  {
    int m_number_of_FileLockAccess_objects;                     // The number of FileLockAccess objects that currently are in use (for this FileLockSingleton).
    boost::interprocess::file_lock m_file_lock;                 // The file lock. Note that this, too, must be protected by a mutex
                                                                // (mostly for POSIX which does not guarantee thread synchronization).
                                                                // The boost documentation advises to use the same thread to lock
                                                                // and unlock a file-- but that is too restrictive imho.
  };
  using Data_ts = threadsafe::Unlocked<Data, threadsafe::policy::Primitive<std::mutex>>;

  Data_ts m_data;                                               // Threadsafe instance of Data, see above.
  std::filesystem::path const m_canonical_path;                 // The (canonical) path to the underlaying lock file.
  std::FILE* m_lock_file;                                       // This points to an open file m_canonical_path once the file lock has been obtained.
                                                                // it is used to write the PID to. We can't close it anymore because that also unlocks
                                                                // the file lock!

 private:
  // Only class FileLock may construct objects of this type.
  // Note that it may only create ONE instance of FileLockSingleton PER
  // canonical path, otherwise this wouldn't be a singleton.
  FileLockSingleton(std::filesystem::path const& canonical_path) : m_canonical_path(canonical_path)
  {
    DoutEntering(dc::notice, "FileLockSingleton(" << canonical_path << ") [" << this << "]");
    bool success = false;
    do
    {
      try
      {
        // Open the file lock (this does not lock it).
        boost::interprocess::file_lock file_lock(canonical_path.c_str());
        // Transfer ownership to us.
        Data_ts::wat data_w(m_data);
        data_w->m_file_lock.swap(file_lock);
        data_w->m_number_of_FileLockAccess_objects = 0;
        success = true;
      }
      catch (boost::interprocess::interprocess_exception& error)
      {
        if (error.get_error_code() != boost::interprocess::not_found_error)
          THROW_ALERTC(error.get_native_error(), "Failed to create file_lock([FILENAME])", AIArgs("[FILENAME]", canonical_path));
        // File doesn't exist, create it and try again.
        std::ofstream lockfile(canonical_path);
        if (!lockfile.is_open())
          THROW_ALERTE("Failed to create lock file [FILENAME].", AIArgs("[FILENAME]", canonical_path));
        Dout(dc::notice, "Created non-existing lockfile " << canonical_path << ".");
      }
    }
    while (!success);
  }

 public:
  ~FileLockSingleton()
  {
    DoutEntering(dc::notice, "~FileLockSingleton() [" << this << "]");
  }

  // Accessor.
  std::filesystem::path const& canonical_path() const
  {
    return m_canonical_path;
  }

  friend void intrusive_ptr_add_ref(FileLockSingleton* p);
  friend void intrusive_ptr_release(FileLockSingleton* p);

#ifdef CWDEBUG
  // Support for printing to debug ostreams.
  void print_on(std::ostream& os, Data_ts::crat const& data_r) const
  {
    os << "{F" << m_canonical_path << ' ';
    if (!data_r->m_number_of_FileLockAccess_objects)
      os << "(unlocked)F}";
    else
    {
      os << "(ref'd " << data_r->m_number_of_FileLockAccess_objects << "), ";
      AIStatefulTask* owning_task = debug_get_owner();
      if (owning_task)
        os << "<owned by [" << owning_task << "]>F}";
      else
        os << "<unowned>F}";
    }
  }
  void print_on(std::ostream& os) const
  {
    print_on(os, m_data);
  }
  friend std::ostream& operator<<(std::ostream& os, FileLockSingleton const& file_lock_singleton)
  {
    os << "FileLockSingleton:";
    file_lock_singleton.print_on(os);
    return os;
  }
#endif
};

// class FileLock
//
// One can create any number of FileLock objects (default constructor). The life time of these
// objects must be larger than any other related object, for example, they can for instance
// (even) be global objects or created at the start of main().
//
// Somewhere at the start of the program, once it is possible to construct the filelock name,
// they can be initialized with their filename -- at most once -- by calling set_filename.
// If this filename would be known at the time of their construction then of course you can
// pass it to the constructor, but otherwise it is ok to pass it later-- but before they are
// actually being used.
//
// FileLock is basically a wrapper around a boost::intrusive_ptr<FileLockSingleton>, along
// with a static std::set<boost::intrusive_ptr<FileLockSingleton>> in order to take care of
// creating and adding the new FileLockSingleton to this std::set whenever a new filelock is
// added (through set_filename), making sure that only one instance of FileLockSingleton is
// created per canonical path.
//
class FileLock
{
 private:
  struct CanonicalPathCompare
  {
    bool operator()(std::shared_ptr<FileLockSingleton> const& p1, std::shared_ptr<FileLockSingleton> const& p2) const
    {
      // This compares if the paths have the same string representation.
      // Therefore it must be guaranteed in somewhere else that a new path is
      // not equivalent (doesn't resolve to the same actual file) before adding
      // it to the set!
      return p1->canonical_path() < p2->canonical_path();
    }
    // Allow direct comparision with std::filesystem::path.
    using is_transparent = std::true_type;
    bool operator()(std::filesystem::path const& canonical_path, std::shared_ptr<FileLockSingleton> const& p2) const
    {
      return canonical_path < p2->canonical_path();
    }
    bool operator()(std::shared_ptr<FileLockSingleton> const& p1, std::filesystem::path const& canonical_path) const
    {
      return p1->canonical_path() < canonical_path;
    }
  };
  using file_lock_map_ts = threadsafe::Unlocked<std::set<std::shared_ptr<FileLockSingleton>, CanonicalPathCompare>, threadsafe::policy::Primitive<std::mutex>>;
  static file_lock_map_ts s_file_lock_map;                              // Global map of all file locks by canonical path.

  // FileLockAccess instances created from this FileLock instance (or another that
  // points to the same FileLockSingleton) also point to the same FileLockSingleton instance.
  // Therefore, the life time of the last FileLock that points to such instance must
  // surpass that of all such FileLockAccess instances (enforced in debug mode with ASSERTs).
  std::shared_ptr<FileLockSingleton> m_file_lock_instance;         // Pointer to underlaying FileLockSingleton.

 public:
  // Default constructor. Use set_filename() to associate the FileLock with an inode.
  FileLock() { }
  // Construct a FileLock that is associated with the inode represented by filename.
  // If the file doesn't exist it is created.
  FileLock(std::filesystem::path const& filename) { set_filename(filename); }
  ~FileLock();

  // Set the file (inode) to use. If the file doesn't exist it is created.
  void set_filename(std::filesystem::path const& filename);

  std::filesystem::path canonical_path() const
  {
    // Don't call this function before calling set_filename().
    ASSERT(m_file_lock_instance);
    return m_file_lock_instance->canonical_path();
  }

 private:
  friend class FileLockAccess;
  std::shared_ptr<FileLockSingleton> const& get_instance() const
  {
    // Associate a FileLock with a path before passing it to a FileLockAccess object.
    ASSERT(m_file_lock_instance);
    return m_file_lock_instance;
  }

 public:
#ifdef CWDEBUG
  void print_on(std::ostream& os) const
  {
    os << "{f" << utils::print_using(m_file_lock_instance, &FileLockSingleton::print_on) << "f}";
  }
  friend std::ostream& operator<<(std::ostream& os, FileLock const& file_lock)
  {
    os << "FileLock:";
    file_lock.print_on(os);
    return os;
  }
#endif
};
