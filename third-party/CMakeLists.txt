project(third-party CXX)

configure_third_party_compiler_flags()

add_subdirectory(bitcoin_utils)
add_subdirectory(miniupnp/miniupnpc)
add_subdirectory(easyloggingpp)
