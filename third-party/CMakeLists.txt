project(third-party CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_third_party_compiler_flags()

add_subdirectory(bitcoin_utils)
add_subdirectory(miniupnp/miniupnpc)
add_subdirectory(easyloggingpp)
