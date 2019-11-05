#include "sys.h"
#include "FileLock.h"
#include <sys/types.h>
#include <unistd.h>

void FileLock::set_filename(boost::filesystem::path const& fname)
{
  // Don't set the filename of a FileLock twice.
  ASSERT(!m_file_lock_instance);
  // Don't try to set an empty filename.
  ASSERT(!fname.empty());

  bool file_exists = boost::filesystem::exists(fname);
  file_lock_map_ts::wat file_lock_map_w(s_file_lock_map);
  if (file_exists)
  {
    // Look if we already have a FileLock with the same or equivalent path.
    boost::system::error_code error_code;
    for (auto iter = file_lock_map_w->begin(); iter != file_lock_map_w->end(); ++iter)
    {
      if (boost::filesystem::equivalent((*iter)->filename(), fname, error_code))
      {
        m_file_lock_instance = *iter;
#ifdef CWDEBUG
        if (fname != (*iter)->filename())
          Dout(dc::warning, "FileLock::set_filename(" << fname << "): " << filename() << " already exists and is the same file!");
#endif
        return;
      }
      else if (error_code)
      {
        Dout(dc::warning, "Error: " << error_code.message());
      }
    }
  }
  else
  {
    // Create the file.
    std::ofstream lockfile(fname);
    if (!lockfile.is_open())
      THROW_ALERTE("Failed to create lock file [FILENAME].", AIArgs("[FILENAME]", fname));
    // Now that it exists we can get the canonical path.
  }
  boost::filesystem::path canonical_path = boost::filesystem::canonical(fname);
  if (!file_exists)
    Dout(dc::notice, "Created non-existing lockfile " << canonical_path << ".");
  // This file is not in our map. Add it.
  auto res = file_lock_map_w->emplace(new FileLockSingleton(canonical_path));
  ASSERT(res.second);
  m_file_lock_instance = *res.first;

  // Sanity check.
  ASSERT(filename() == canonical_path);
}

FileLock::~FileLock()
{
  if (!m_file_lock_instance)
    return;
#if 0
  file_lock_map_ts::wat file_lock_map_w(s_file_lock_map);
  auto iter = file_lock_map_w->find(m_file_lock_instance->filename());
  ASSERT(iter != file_lock_map_w->end());
  if (iter->second.use_count() == 2) // The one in the std::map and our own.
    file_lock_map_w->erase(iter);
#endif
}

//static
FileLock::file_lock_map_ts FileLock::s_file_lock_map;

void intrusive_ptr_add_ref(FileLockSingleton* p)
{
  FileLockSingleton::Data_ts::wat data_w(p->m_data);
  if (data_w->m_ref_count++ == 0)
  {
    boost::filesystem::path const filename = p->filename();
    if (!data_w->m_file_lock.try_lock())
    {
      data_w->m_ref_count = 0;
      // If another process has the file lock, then we don't block but instead throw an error.
      // Note that throwing aborts the constructor (of DatabaseFileLock) causing its destructor
      // not to be called, and therefore automatically guarantees that the corresponding
      // intrusive_ptr_release won't be called.
      THROW_MALERT("Failed to obtain file lock [FILENAME]: is it already locked by some other process?", AIArgs("FILENAME", filename));
    }
    try
    {
      std::fstream lockfile(filename, std::ios_base::in|std::ios_base::out|std::ios_base::binary);
      if (!lockfile.is_open())
        THROW_ALERTE("Failed to open lock file [FILENAME].", AIArgs("[FILENAME]", filename));
      lockfile.exceptions(std::fstream::badbit);

      pid_t pid = getpid();
      pid_t lastpid;
      // Read the PID of the last process that obtained the file lock.
      if (!(lockfile.read(reinterpret_cast<char*>(&lastpid), sizeof(lastpid)) && lockfile.gcount() == sizeof(lastpid) && lastpid == pid))
      {
        lockfile.clear();
        // Write our PID to the file if it wasn't already in there.
        lockfile.seekg(0);      // Rewind.
        if (!lockfile.write(reinterpret_cast<char const*>(&pid), sizeof(pid)))
          Dout(dc::warning, "Could not write PID to the lock file " << filename << "!");
      }
      Dout(dc::notice, "Obtained file lock " << print_using(*p, [&data_w](std::ostream& os, FileLockSingleton const& fls){ fls.print_on(os, data_w); }));
#if 0 // FIXME
      // Flush the in-memory caches of the database, because they cannot be trusted anymore.
      Dout(dc::primbackup, "Obtained FILE lock for 'uploads' database, PID " << pid << ". Flushing in-memory cache of database...");
      DatabaseFileLock file_lock;					// Calls this same function again and recursively locks p->mData a second time.
      DatabaseAIStatefulTaskLock stateful_task_lock(file_lock);
      ScopedBlockingBackEndAccess back_end_access(stateful_task_lock);
      back_end_access->clear_memory_cache();
#endif
    }
    catch (std::ios_base::failure const& error) // Thrown by std::fstream.
    {
      THROW_ALERTC(error.code(), "Failed to open lock file [FILENAME]: [MESSAGE]", AIArgs("[FILENAME]", filename)("[MESSAGE]", error.what()));
    }
  }
}

void intrusive_ptr_release(FileLockSingleton* p)
{
  FileLockSingleton::Data_ts::wat data_w(p->m_data);
  if (--data_w->m_ref_count == 0)
  {
    data_w->m_file_lock.unlock();
    Dout(dc::notice, "Released file lock " << print_using(p, &FileLockSingleton::print_on) << ".");
  }
}

void intrusive_ptr_add_ref(AIStatefulTaskLockSingleton* p)
{
  AIStatefulTaskLockSingleton::Data_ts::wat data_w(p->m_data);
  int prev_count = data_w->m_ref_count++;
  // We should never get here when p isn't already owned, because we can't set the owner from here.
  // Grabbing the ownership (and incrementing the ref_countt to 1) is done from AIStatefulTaskLockSingleton::try_lock().
  ASSERT(prev_count > 0);
}

void intrusive_ptr_release(AIStatefulTaskLockSingleton* p)
{
  AIStatefulTaskLockSingleton::Data_ts::wat data_w(p->m_data);
  if (--data_w->m_ref_count == 0)
  {
    Dout(dc::notice, data_w->m_owner << " released stateful task lock.");
    data_w->m_owner = nullptr;
  }
}

boost::intrusive_ptr<AIStatefulTaskLockSingleton> AIStatefulTaskLockSingleton::try_lock(AIStatefulTask const* owner)
{
  boost::intrusive_ptr<AIStatefulTaskLockSingleton> res;
  bool already_owned;
  {
    Data_ts::wat data_w(m_data);
    already_owned = data_w->m_ref_count > 0;
    if (!already_owned)
    {
      data_w->m_ref_count = 1;
      data_w->m_owner = owner;
    }
  }
  if (!already_owned)
  {
    Dout(dc::notice, owner << " obtained stateful task lock.");
    res = this;                                 // This increments the ref_count to 2.
    Data_ts::wat(m_data)->m_ref_count = 1;      // So, set it back to 1.
  }
  return res;
}

char const* AIStatefulTaskLockTask::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(AIStatefulTaskLockTask_trylock);
  }
  ASSERT(false);
  return "UNKNOWN STATE";
}

void AIStatefulTaskLockTask::abort_impl()
{
}

//AIStatefulTaskLockAccess stateful_task_lock_access(file_lock_access);
void AIStatefulTaskLockTask::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case AIStatefulTaskLockTask_trylock:
    {
      bool lock_obtained;
      while (!(lock_obtained = m_stateful_task_lock_access.try_lock(this)))
      {
        // FIXME wait_until([](){ return !owner; }, 1);
        break;
      }
      if (lock_obtained)
        finish();
      break;
    }
  }
}
