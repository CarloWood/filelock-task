#include "statefultask/AIStatefulTask.h"
#include "threadsafe/aithreadsafe.h"
#include "utils/AIAlert.h"
#include "utils/Singleton.h"
#include "debug.h"
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/filesystem.hpp>
#include <string>
#include <set>
#include <fstream>

#pragma once

class FileLockSingleton;
class FileLock;
class FileLockAccess;
class AIStatefulTaskNamedMutex;

// A per-file-lock manager for stateful task locking.
//
// Helper class.
//
// Manages which AIStatefulTask owns the lock.
//
// The user can not create a AIStatefulTaskLockSingleton, instead use class FileLock / FileLockAccess.
//
class AIStatefulTaskLockSingleton
{
 private:
  struct Data
  {
    int m_ref_count;                    // The number of boost::intrusive_ptr<AIStatefulTaskLockSingleton> objects that point to this instance.
    AIStatefulTask const* m_owner;      // The current owner of this lock (m_ref_count > 0), or nullptr when none (m_ref_count == 0).

    Data() : m_ref_count(0), m_owner(nullptr) { }
  };
  using Data_ts = aithreadsafe::Wrapper<Data, aithreadsafe::policy::Primitive<std::mutex>>;

  Data_ts m_data;                       // Set when some AIStatefulTask grabbed ownership of the lock, and reset when it was released.

 private:
  friend class FileLockSingleton;
  AIStatefulTaskLockSingleton() = default;
  AIStatefulTaskLockSingleton(AIStatefulTaskLockSingleton const&) = delete;

 public:
  bool is_owner(AIStatefulTask const* owner) const { return Data_ts::crat(m_data)->m_owner == owner; }

 protected:
  friend void intrusive_ptr_add_ref(AIStatefulTaskLockSingleton* p);
  friend void intrusive_ptr_release(AIStatefulTaskLockSingleton* p);

  // The functions below are protected because they may only be accessed by FileLockAccess.
  // The reason for this is to guarantee that the process also holds a FileLock before trying
  // to obtain the stateful task lock.
  friend class FileLockAccess;

  // Returns a valid pointer if the lock was obtained, or null if the mutex is already
  // locked by another stateful task. To unlock, just destruct the intrusive_ptr.
  boost::intrusive_ptr<AIStatefulTaskLockSingleton> try_lock(AIStatefulTask const* owner);

 public:
  void print_on(std::ostream& os) const
  {
    Data_ts::crat data_r(m_data);
    if (!data_r->m_ref_count)
      os << "{unowned}";
    else
      os << "{T owned by " << data_r->m_owner << " (ref'd " << data_r->m_ref_count << ")T}";
  }
  friend std::ostream& operator<<(std::ostream& os, AIStatefulTaskLockSingleton const& stateful_task_lock_singleton)
  {
    os << "AIStatefulTaskLockSingleton:";
    stateful_task_lock_singleton.print_on(os);
    return os;
  }
};

// Helper class for FileLock.
//
// An object of this type contains the "canonical" path (that is, the *first* path used for a
// FileLock object, passed through boost::filesystem::absolute(filename).lexically_normal(),
// of all (subsequent) 'boost::filesystem::equivalent' paths -- not the result of
// boost::filesystem::canonical which also removes all symbolic links), the aithreadsafe
// boost::interprocess::file_lock instance with a reference count of the number of FileLockAccess
// objects pointing to this instance, and an instance of AIStatefulTaskLockSingleton: a reference
// counted pointer to the AIStatefulTask that owns the lock, if any.
//
// There is only one instance per canonical path (read: inode) of this class.
// The user can not create a FileLockSingleton; instead use class FileLock and FileLockAccess.
//
class FileLockSingleton
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
  using Data_ts = aithreadsafe::Wrapper<Data, aithreadsafe::policy::Primitive<std::mutex>>;

  Data_ts m_data;                                               // Threadsafe instance of Data, see above.
  boost::filesystem::path const m_canonical_path;               // The (canonical) path to the underlaying lock file.
  AIStatefulTaskLockSingleton m_stateful_task_lock_instance;    // The task that owns this file lock, if any.
  std::FILE* m_lock_file;                                       // This points to an open file m_canonical_path once the file lock has been obtained.
                                                                // it is used to write the PID to. We can't close it anymore because that also unlocks
                                                                // the file lock!

 private:
  // Only class FileLock may construct objects of this type.
  // Note that it may only create ONE instance of FileLockSingleton PER
  // canonical path, otherwise this wouldn't be a singleton.
  FileLockSingleton(boost::filesystem::path const& canonical_path) : m_canonical_path(canonical_path)
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
  boost::filesystem::path const& canonical_path() const
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
      os << "(ref'd " << data_r->m_number_of_FileLockAccess_objects << "), " <<
        utils::print_using(m_stateful_task_lock_instance, &AIStatefulTaskLockSingleton::print_on) << "F}";
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
    // Allow direct comparision with boost::filesystem::path.
    using is_transparent = std::true_type;
    bool operator()(boost::filesystem::path const& canonical_path, std::shared_ptr<FileLockSingleton> const& p2) const
    {
      return canonical_path < p2->canonical_path();
    }
    bool operator()(std::shared_ptr<FileLockSingleton> const& p1, boost::filesystem::path const& canonical_path) const
    {
      return p1->canonical_path() < canonical_path;
    }
  };
  using file_lock_map_ts = aithreadsafe::Wrapper<std::set<std::shared_ptr<FileLockSingleton>, CanonicalPathCompare>, aithreadsafe::policy::Primitive<std::mutex>>;
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
  FileLock(boost::filesystem::path const& filename) { set_filename(filename); }
  ~FileLock();

  // Set the file (inode) to use. If the file doesn't exist it is created.
  void set_filename(boost::filesystem::path const& filename);

  boost::filesystem::path canonical_path() const
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

// Locking the file lock.
//
// Once one has a FileLock initialized with a filename - it can be passed to the constructor of a
// FileLockAccess object which will lock the underlaying file lock of the filename passed to the
// used FileLock.
//
// This should work any number of times when used by the same process (but not by different processes).
// The number of existing FileLockAccess objects is just reference counted and the file lock
// will be released only after the last FileLockAccess is released. Hence, only creating the first
// FileLockAccess costs time - subsequent instances can be created very fast since that just increments
// a reference counter.
//
class FileLockAccess
{
 private:
#if CW_DEBUG
  std::weak_ptr<FileLockSingleton> m_debug_weak_ptr;
#endif
  boost::intrusive_ptr<FileLockSingleton> m_file_lock_ptr;

 public:
  FileLockAccess(FileLock& file_lock) : DEBUG_ONLY(m_debug_weak_ptr(file_lock.get_instance()),) m_file_lock_ptr(file_lock.get_instance().get()) { }
  FileLockAccess(FileLock&& file_lock) : DEBUG_ONLY(m_debug_weak_ptr(file_lock.get_instance()),) m_file_lock_ptr(file_lock.get_instance().get())
  {
    // The FileLock object passed to this constructor cannot be the only FileLock object.
    // You need to keep one around with a much longer lifetime.
    ASSERT(m_debug_weak_ptr.use_count() > 2);
  }

#if CW_DEBUG
  // The default copy constructor suffices, but this one has a debug check builtin.
  FileLockAccess(FileLockAccess const& file_lock_access) :
    m_debug_weak_ptr(file_lock_access.debug_weak_ptr()), m_file_lock_ptr(file_lock_access.m_file_lock_ptr) { }

 public:
  std::weak_ptr<FileLockSingleton> const& debug_weak_ptr() const
  {
    // The lifetime of at least one FileLock object, of the corresponding canonical
    // path, must be longer than the lifetime of FileLockAccess objects created from it.
    // I.e. you deleted the FileLock object corresponding to this FileLockAccess. Don't do that.
    ASSERT(!m_debug_weak_ptr.expired());
    return m_debug_weak_ptr;
  }
#endif

 protected:
  friend class AIStatefulTaskNamedMutex;
  boost::intrusive_ptr<AIStatefulTaskLockSingleton> try_lock(AIStatefulTask const* owner)
  {
    // See the assert in debug_weak_ptr().
    ASSERT(!m_debug_weak_ptr.expired());
    return m_file_lock_ptr->m_stateful_task_lock_instance.try_lock(owner);
  }

 public:
#ifdef CWDEBUG
  void print_on(std::ostream& os) const
  {
    if (m_debug_weak_ptr.expired())
      os << "*{deleted FileLockSingleton}";
    else
      // This prints for example:
      //  {*{"ai-statefultask-testsuite/filelock-task/flock1" (ref'd 2), {owned by 0x17309c0 (ref'd 1)}}}
      os << "{a" << utils::print_using(m_file_lock_ptr, &FileLockSingleton::print_on) << "a}";
  }
  friend std::ostream& operator<<(std::ostream& os, FileLockAccess const& file_lock_access)
  {
    os << "FileLockAccess:";
    file_lock_access.print_on(os);
    return os;
  }
#endif
};

// Once a FileLockAccess has been created, it can be used to create an AIStatefulTaskNamedMutex object.
class AIStatefulTaskNamedMutex
{
 private:
  FileLockAccess m_file_lock_access;                                            // Kept to increment the reference count of FileLockSingleton.
  boost::intrusive_ptr<AIStatefulTaskLockSingleton> m_stateful_task_lock_ref;   // Kept to increment the reference count of AIStatefulTaskLockSingleton.

 public:
  // Create a AIStatefulTaskNamedMutex that already has the file lock locked.
  AIStatefulTaskNamedMutex(FileLockAccess const& file_lock_access) noexcept : m_file_lock_access(file_lock_access) { }
  // Create a AIStatefulTaskNamedMutex directly from a FileLock (this will try to lock the file lock if it isn't locked already).
  // Using a reference here because the FileLock that is passed should not be a temporary.
  AIStatefulTaskNamedMutex(FileLock& file_lock) : m_file_lock_access(file_lock) { }
  // Create a AIStatefulTaskNamedMutex directly from an rvalue reference to a FileLock.
  // In debug mode this requires that some other FileLock object also exists.
  AIStatefulTaskNamedMutex(FileLock&& file_lock) : m_file_lock_access(std::move(file_lock)) { }

#if 0
  // Actually, it's also ok when LockedBackEnd wants to create a temporary AIStatefulTaskNamedMutex to access itself...
  // Note that this may only be automatic variables (ie, not a member of LockedBackEnd), or else the database would be locked forever.
  friend class LockedBackEnd;
  AIStatefulTaskNamedMutex(LockedBackEnd*) : m_statefuL_task_lock(AIStatefulTaskLock::getInstance()) { }

  // It's also ok for ScopedBlockingAIStatefulTaskNamedMutex, which is derived from AIStatefulTaskNamedMutex, to be constructed
  // from just a FileLockAccess - because the constructor of ScopedBlockingAIStatefulTaskNamedMutex calls lock().
  friend class ScopedBlockingAIStatefulTaskNamedMutex;
#endif

  bool try_lock(AIStatefulTask const* owner)
  {
    DoutEntering(dc::notice, "AIStatefulTaskNamedMutex::try_lock(" << owner << ") [" << this << "]");
    m_stateful_task_lock_ref = m_file_lock_access.try_lock(owner);
    bool is_locked = static_cast<bool>(m_stateful_task_lock_ref);
    Dout(dc::notice, (is_locked ? "Successfully locked." : "try_lock failed."));
    return is_locked;
  }

  bool is_locked() const { return static_cast<bool>(m_stateful_task_lock_ref); }

  void unlock(AIStatefulTask const* owner)
  {
    DoutEntering(dc::notice, "AIStatefulTaskNamedMutex::unlock(" << owner << ") [" << this << "]");
    ASSERT(is_locked() && m_stateful_task_lock_ref->is_owner(owner));
    m_stateful_task_lock_ref.reset();
  }

//  LockedBackEnd* operator->() const;

  // Accessor.
  FileLockAccess const& file_lock() const { return m_file_lock_access; }

 public:
  void print_on(std::ostream& os) const
  {
    os << "{m" << utils::print_using(m_file_lock_access, &FileLockAccess::print_on) << ", ";
    if (is_locked())
      os << utils::print_using(m_stateful_task_lock_ref, &AIStatefulTaskLockSingleton::print_on) << "m}";
    else
      os << "<unlocked>m}";
  }
  friend std::ostream& operator<<(std::ostream& os, AIStatefulTaskNamedMutex const& task_lock_access)
  {
    os << "AIStatefulTaskNamedMutex:";
    task_lock_access.print_on(os);
    return os;
  }
};

class AIStatefulTaskLockTask : public AIStatefulTask
{
 protected:
  using direct_base_type = AIStatefulTask;

  enum stateful_task_lock_task_state_type {
    AIStatefulTaskLockTask_trylock = direct_base_type::max_state        // The first and only state.
  };

 private:
  AIStatefulTaskNamedMutex m_stateful_task_lock_access;

 public:
  AIStatefulTaskLockTask(FileLockAccess const& file_lock) : AIStatefulTask(CWDEBUG_ONLY(true)), m_stateful_task_lock_access(file_lock) { }
  static state_type constexpr max_state = AIStatefulTaskLockTask_trylock + 1;

 protected:
  char const* state_str_impl(state_type run_state) const override;
  void multiplex_impl(state_type run_state) override;
  void abort_impl() override;       // Default does nothing.
};
