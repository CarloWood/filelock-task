/**
 * @file
 * @brief Connect to an end point. Declaration of class TaskLock.
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

#include "statefultask/AIStatefulTask.h"
#include "AIStatefulTaskNamedMutex.h"
#include "debug.h"

namespace task {

class TaskLock : public AIStatefulTask
{
 protected:
  using direct_base_type = AIStatefulTask;

  enum stateful_task_lock_task_state_type {
    TaskLock_lock = direct_base_type::state_end,       // The first state.
    TaskLock_locked
  };

 private:
  FileLockAccess m_file_lock_access;

 public:
  TaskLock(FileLockAccess const& file_lock_access) :
    AIStatefulTask(CWDEBUG_ONLY(true)), m_file_lock_access(file_lock_access) {
      DoutEntering(dc::statefultask, "TaskLock(" << file_lock_access << ") [" << this << "]"); }

  ~TaskLock() { DoutEntering(dc::statefultask, "~TaskLock() [" << this << "]"); }

  static state_type constexpr state_end = TaskLock_locked + 1;

  void unlock()
  {
    m_file_lock_access.unlock_task();
  }

 private:
  bool lock(AIStatefulTask::condition_type condition) { return m_file_lock_access.lock_task(this, condition); }
  char const* state_str_impl(state_type run_state) const final override;
  void multiplex_impl(state_type run_state) final override;
};

} // namespace task
