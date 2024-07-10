cmake_minimum_required(VERSION 3.15...3.30)

project(filelock-task
  DESCRIPTION "A statefultask-based file lock task"
  LANGUAGES CXX
)

include(AICxxProject)

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(filelock-task_ObjLib OBJECT)

# The list of source files.
target_sources(filelock-task_ObjLib
  PRIVATE
    "FileLock.cxx"
    "TaskLock.cxx"

    "AIStatefulTaskNamedMutex.h"
    "FileLockAccess.h"
    "FileLock.h"
    "TaskLock.h"
)

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(filelock-task_ObjLib
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++17.
target_compile_features( filelock-task_ObjLib
  PUBLIC cxx_std_17
)

# Set link dependencies.
target_link_libraries( filelock-task_ObjLib
  PUBLIC
    AICxx::statefultask
)

# Create an ALIAS target.
add_library(AICxx::filelock-task ALIAS filelock-task_ObjLib)
