project(third-party CXX)

configure_third_party_compiler_flags()

set(UPNPC_BUILD_SHARED FALSE CACHE BOOL "Build shared library" FORCE)
set(UPNPC_BUILD_TESTS FALSE CACHE BOOL "Build test executables" FORCE)
set(UPNPC_BUILD_SAMPLE FALSE CACHE BOOL "Build sample executables" FORCE)
set(UPNPC_NO_INSTALL TRUE CACHE BOOL "Disable installation" FORCE)
add_subdirectory(miniupnp/miniupnpc)

add_subdirectory(easyloggingpp)

set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "Build LevelDB's unit tests" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "Build LevelDB's benchmarks" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "Install LevelDB's header and library" FORCE)
add_subdirectory(leveldb)
