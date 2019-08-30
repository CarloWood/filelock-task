#include "FileLock.h"

void FileLock::set_filename(std::string const& filename)
{
  // Don't set the filename of a FileLock twice.
  ASSERT(m_filename.empty());
  // Don't try to set an empty filename.
  ASSERT(!filename.empty());

  m_filename = filename;
  filename_map_ts::wat filename_map_w(m_filename_map);
  auto iter = filename_map_w->find(filename);
  if (iter == filename_map_w->end())
    iter = filename_map_w->emplace(filename, new FileLockSingleton(filename)).first;
  m_file_lock = iter->second;
}

FileLock::~FileLock()
{
  if (m_filename.empty())
    return;
  filename_map_ts::wat filename_map_w(m_filename_map);
  auto iter = filename_map_w->find(m_filename);
  ASSERT(iter != filename_map_w->end());
  if (iter->second.use_count() == 1) // 2?
    filename_map_w->erase(iter);
}
