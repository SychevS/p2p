project(third-party CXX)

configure_third_party_compiler_flags()

add_subdirectory(bitcoin_utils)

set(UPNPC_BUILD_SHARED FALSE CACHE BOOL "Build shared library" FORCE)
set(UPNPC_BUILD_TESTS FALSE CACHE BOOL "Build test executables" FORCE)
set(UPNPC_BUILD_SAMPLE FALSE CACHE BOOL "Build sample executables" FORCE)
set(UPNPC_NO_INSTALL TRUE CACHE BOOL "Disable installation" FORCE)
add_subdirectory(miniupnp/miniupnpc)

add_subdirectory(easyloggingpp)
