#pragma once

#include "FileLock.h"

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

#if 0
 protected:
  friend class AIStatefulTaskNamedMutex;
  boost::intrusive_ptr<AIStatefulTaskLockSingleton> try_lock(AIStatefulTask const* owner)
  {
    // See the assert in debug_weak_ptr().
    ASSERT(!m_debug_weak_ptr.expired());
    return m_file_lock_ptr->m_stateful_task_lock_instance.try_lock(owner);
  }
#endif

 public:
  bool lock_task(AIStatefulTask* task, AIStatefulTask::condition_type condition)
  {
    return m_file_lock_ptr->lock(task, condition);
  }

  void unlock_task()
  {
    m_file_lock_ptr->unlock();
  }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const
  {
    if (m_debug_weak_ptr.expired())
      os << "*{deleted FileLockSingleton}";
    else
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
