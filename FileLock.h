#include "threadsafe/aithreadsafe.h"
#include "utils/AIAlert.h"
#include "debug.h"
#include <boost/interprocess/sync/file_lock.hpp>
#include <string>
#include <map>

#pragma once

class FileLock;

// Helper class for FileLock.
class FileLockSingleton
{
 private:
  boost::interprocess::file_lock m_file_lock;

 private:
  friend class FileLock;
  FileLockSingleton(std::string const& filename)
  {
    DoutEntering(dc::notice, "FileLockSingleton(\"" << filename << "\") [" << this << "]");
    bool success = false;
    do
    {
      try
      {
        // Open the file lock (this does not lock it).
        boost::interprocess::file_lock file_lock(filename.c_str());
        // Transfer ownership to us.
        m_file_lock.swap(file_lock);
        success = true;
      }
      catch (boost::interprocess::interprocess_exception& error)
      {
        if (error.get_error_code() != boost::interprocess::not_found_error)
          THROW_ALERTC(error.get_native_error(), "Failed to open lock file \"[FILENAME]\"", AIArgs("[FILENAME]", filename));
        // File doesn't exist, create it and try again.
        std::ofstream lockfile(filename);
        if (!lockfile.is_open())
          THROW_ALERTE("Failed to create lock file \"[FILENAME]\".", AIArgs("[FILENAME]", filename));
      }
    }
    while (!success);
  }

  ~FileLockSingleton()
  {
    DoutEntering(dc::notice, "~FileLockSingleton() [" << this << "]");
  }
};

// class FileLock
//
// One can create any number of FileLock objects (default constructor). The life time of these
// objects must be larger than any other related object, for example, they could (even) be global
// objects, or created at the start of main().
//
// Somewhere at the start of the program, once it is possible to construct the file lock name,
// they can be initialized with their file name -- at most once. If this file name would be
// known at the time of their construction then of course you can pass it to the constructor,
// but otherwise it is ok to pass it later - but before they are actually being used.
//
// It is not allowed to use the same filename for two different instances.
//
class FileLock
{
 private:
  using filelock_map_ts = aithreadsafe::Wrapper<std::map<std::string, std::shared_ptr<FileLockSingleton>>, aithreadsafe::policy::Primitive<std::mutex>>;
  static filelock_map_ts m_filelock_map;                // Global map of all file locks by name.

  std::shared_ptr<FileLockSingleton> m_file_lock;       // Pointer to underlaying FileLockSingleton.

 public:
  FileLock() { }
  ~FileLock();
  FileLock(std::string const& filename) { set_filename(filename); }

  void set_filename(std::string const& filename);

 private:
  friend class FileLockAccessor;

};

class FileLockAccessor
{

};
