/**
 * filelock-task -- A statefultask-based file lock task.
 *
 * @file
 * @brief Declaration of class FileLock.
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
#include "FileLock.h"
#include <sys/types.h>
#include <unistd.h>

void FileLock::set_filename(std::filesystem::path const& filename)
{
  // Don't try to set an empty filename.
  ASSERT(!filename.empty());
  std::filesystem::path normal_path = std::filesystem::absolute(filename).lexically_normal();

  {
    file_lock_map_ts::wat file_lock_map_w(s_file_lock_map);

    // Don't set the filename of a FileLock twice.
    ASSERT(!m_file_lock_instance);

    // Look if we already have a FileLock with the same or equivalent path.
    std::error_code error_code;
    for (auto iter = file_lock_map_w->begin(); iter != file_lock_map_w->end(); ++iter)
    {
      if (std::filesystem::equivalent((*iter)->canonical_path(), normal_path, error_code))
      {
        m_file_lock_instance = *iter;
#ifdef CWDEBUG
        if (normal_path != (*iter)->canonical_path())
          Dout(dc::warning, "FileLock::set_filename(" << filename << "): " << canonical_path() << " already exists and is the same file!");
#endif
        return;
      }
      else if (error_code)
      {
        Dout(dc::warning, "Error: " << error_code.message());
      }
    }
    // This file is not in our map. Add it.
    auto res = file_lock_map_w->emplace(new FileLockSingleton(normal_path));
    ASSERT(res.second);
    m_file_lock_instance = *res.first;

  } // Unlock s_file_lock_map.

  // Sanity check.
  // Note: our canonical means that it is the name stored in s_file_lock_map for that inode.
  // However it can contain symbolic links: it is merely the (lexically normalized) path that
  // was passed (first) to set_filename(). Lexically normalized means that occurances of '.'
  // and '..' where removed from the path, but not symbolic links, if any. Boost filesystem
  // also uses the word 'canonical' in which case they also remove symbolic links, but that
  // is not how we use it.
  ASSERT(canonical_path() == normal_path);
}

FileLock::~FileLock()
{
  if (!m_file_lock_instance)
    return;
  file_lock_map_ts::wat file_lock_map_w(s_file_lock_map);
  auto iter = file_lock_map_w->find(m_file_lock_instance->canonical_path());
  ASSERT(iter != file_lock_map_w->end());
  if (iter->use_count() == 2)           // The one in the std::set and our own.
  {
    // Do not destruct the last FileLock object that refers to a given FileLockSingleton
    // (aka, canonical path of a file lock) while a FileLockAccess object for that still exists.
    // That includes therefore AIStatefulTaskNamedMutex and AIStatefulTaskLockTask objects.
    ASSERT(FileLockSingleton::Data_ts::rat(iter->get()->m_data)->m_number_of_FileLockAccess_objects == 0);
    file_lock_map_w->erase(iter);
  }
}

//static
FileLock::file_lock_map_ts FileLock::s_file_lock_map;

void intrusive_ptr_add_ref(FileLockSingleton* p)
{
  FileLockSingleton::Data_ts::wat data_w(p->m_data);
  if (data_w->m_number_of_FileLockAccess_objects++ == 0)
  {
    // Try to obtain the file lock.
    bool const obtained_lock = data_w->m_file_lock.try_lock();

    // (Try to) open file for reading from the start, and writing, in binary mode.
    // Note that is extremely unlikely to fail when locking it succeeded (obtained_lock is true).
    std::filesystem::path const canonical_path = p->canonical_path();
    std::FILE* lock_file_stream = std::fopen(canonical_path.c_str(), "r+b");

    // When either failed - we will throw. Reset m_number_of_FileLockAccess_objects before doing so.
    if (!obtained_lock || !lock_file_stream)
      data_w->m_number_of_FileLockAccess_objects = 0;

    // Bail out when opening the lock file failed, but only when could lock the file at first (the unlikely case).
    if (obtained_lock && !lock_file_stream)
      THROW_ALERTE("Failed to open lock file [FILENAME] after locking it?!", AIArgs("[FILENAME]", canonical_path));

    // Read the PID of the last process that obtained the file lock.
    pid_t lastpid = 0;  // Use 0 for 'unknown' (that would be swapper or sched).
    // Note that reading the PID here, without having the file lock (when obtained_lock is false thus),
    // is a race condition; but in that case the result of this if statement only determines the text
    // of the exception we're going to throw; so all is fine.
    if (lock_file_stream && std::fread(reinterpret_cast<char*>(&lastpid), sizeof(lastpid), 1, lock_file_stream) != 1)
      lastpid = 0;      // Reading PID failed.

    // Bail out when locking the lock file failed.
    if (!obtained_lock)
    {
      if (lock_file_stream)
        std::fclose(lock_file_stream);
      // If another process has the file lock, then we don't block but instead throw an error.
      // Note that throwing aborts the constructor (of DatabaseFileLock) causing its destructor
      // not to be called, and therefore automatically guarantees that the corresponding
      // intrusive_ptr_release won't be called.
      if (lastpid)
        THROW_MALERT("Failed to obtain file lock [FILENAME]: it appears to be locked by process [PID].", AIArgs("FILENAME", canonical_path)("[PID]", lastpid));
      else
        THROW_MALERT("Failed to obtain file lock [FILENAME]: is it already locked by some other process?", AIArgs("FILENAME", canonical_path));
    }

    Dout(dc::notice, "Obtained file lock " << print_using(*p, [&data_w](std::ostream& os, FileLockSingleton const& fls){ fls.print_on(os, data_w); }));

    // Write our PID to the file if it wasn't already in there.
    pid_t pid = getpid();
    if (lastpid != pid)
    {
      std::rewind(lock_file_stream);
      if (std::fwrite(reinterpret_cast<char const*>(&pid), sizeof(pid), 1, lock_file_stream) != 1)
        Dout(dc::warning, "Could not write PID to the lock file " << canonical_path << "!");
      // We can't close the file as that would UNLOCK the boost::interprocess::file_lock!
      p->m_lock_file = lock_file_stream;        // So we can close the file later.
      // But we must flush the data asap.
      std::fflush(lock_file_stream);
    }

#if 0 // FIXME
    // Flush the in-memory caches of the database, because they cannot be trusted anymore.
    Dout(dc::primbackup, "Obtained FILE lock for 'uploads' database, PID " << pid << ". Flushing in-memory cache of database...");
    DatabaseFileLock file_lock;					// Calls this same function again and recursively locks p->mData a second time.
    DatabaseAIStatefulTaskLock stateful_task_lock(file_lock);
    ScopedBlockingBackEndAccess back_end_access(stateful_task_lock);
    back_end_access->clear_memory_cache();
#endif
  }
}

void intrusive_ptr_release(FileLockSingleton* p)
{
  FileLockSingleton::Data_ts::wat data_w(p->m_data);
  // Bug in this library!
  ASSERT(data_w->m_number_of_FileLockAccess_objects > 0);
  if (--data_w->m_number_of_FileLockAccess_objects == 0)
  {
    data_w->m_file_lock.unlock();
    ASSERT(p->m_lock_file);
    std::fclose(p->m_lock_file);
    p->m_lock_file = nullptr;
    Dout(dc::notice, "Released file lock " << print_using(p, [&data_w](std::ostream& os, FileLockSingleton const& fls){ fls.print_on(os, data_w); }) << ".");
  }
}
