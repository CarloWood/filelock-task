/**
 * @file
 * @brief Connect to an end point. Declaration of class AIStatefulTaskNamedMutex.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "statefultask/AIStatefulTaskMutex.h"
#include "FileLockAccess.h"

// Once a FileLockAccess has been created, it can be used to create an AIStatefulTaskNamedMutex object.
class AIStatefulTaskNamedMutex : public AIStatefulTaskMutex
{
 private:
  FileLockAccess m_file_lock_access;            // Kept to increment the reference count of FileLockSingleton.

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

//  LockedBackEnd* operator->() const;

  // Accessor.
  FileLockAccess const& file_lock() const { return m_file_lock_access; }

 public:
#ifdef CWDEBUG
  void print_on(std::ostream& os) const
  {
    os << "{m" << utils::print_using(m_file_lock_access, &FileLockAccess::print_on) << ", ";
    AIStatefulTask* owner = debug_get_owner();
    if (owner)
      os << "owned by [" << owner << "]" << "m}";
    else
      os << "<unlocked>m}";
  }
  friend std::ostream& operator<<(std::ostream& os, AIStatefulTaskNamedMutex const& task_lock_access)
  {
    os << "AIStatefulTaskNamedMutex:";
    task_lock_access.print_on(os);
    return os;
  }
#endif
};
