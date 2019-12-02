/**
 * filelock-task -- A statefultask-based file lock task.
 *
 * @file
 * @brief Definition of class TaskLock.
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

#include "sys.h"
#include "TaskLock.h"

namespace task {

char const* TaskLock::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(TaskLock_lock);
    AI_CASE_RETURN(TaskLock_locked);
  }
  ASSERT(false);
  return "UNKNOWN STATE";
}

void TaskLock::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case TaskLock_lock:
      set_state(TaskLock_locked);
      if (!lock(1))
      {
        wait(1);
        break;
      }
      [[fallthrough]];
    case TaskLock_locked:
      finish();
      break;
  }
}

} // namespace task
