# Initial cache for the local C++20 build. Upstream CMake files stay unchanged.
set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ language standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require the selected C++ standard" FORCE)
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Disable compiler-specific C++ extensions" FORCE)
set(CMAKE_CXX_FLAGS "/std:c++20 /Zc:gotoScope-" CACHE STRING "Global C++ compiler flags" FORCE)
