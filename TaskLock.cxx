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
