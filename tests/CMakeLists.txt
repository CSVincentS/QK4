# QK4 Unit Tests
# Uses Qt Test framework for test infrastructure

# RadioState CAT command parser tests
add_executable(test_radiostate test_radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
)
target_include_directories(test_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate Qt6::Core Qt6::Test)
add_test(NAME RadioStateTests COMMAND test_radiostate)

# RadioState command-registry structural invariant tests
add_executable(test_radiostate_registry test_radiostate_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
)
target_include_directories(test_radiostate_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate_registry Qt6::Core Qt6::Test)
add_test(NAME RadioStateRegistryTests COMMAND test_radiostate_registry)

# RadioState::parseCATCommand() dispatch microbenchmark — not wired into
# add_test() so it doesn't run during every `ctest` invocation; build and
# invoke explicitly when checking for perf regressions.
add_executable(bench_radiostate bench_radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
)
target_include_directories(bench_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(bench_radiostate Qt6::Core Qt6::Test)

# RadioUtils shared utility tests
add_executable(test_radioutils test_radioutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
)
target_include_directories(test_radioutils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radioutils Qt6::Core Qt6::Test)
add_test(NAME RadioUtilsTests COMMAND test_radioutils)

# Protocol packet parsing tests
add_executable(test_protocol test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
)
target_include_directories(test_protocol PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_protocol Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME ProtocolTests COMMAND test_protocol)

# CatServer command handler tests
add_executable(test_catserver test_catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
)
target_include_directories(test_catserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_catserver Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CatServerTests COMMAND test_catserver)

# DX Cluster spot parsing tests
add_executable(test_dxcluster test_dxcluster.cpp
    ${CMAKE_SOURCE_DIR}/src/network/dxclusterclient.cpp
)
target_include_directories(test_dxcluster PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dxcluster Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME DxClusterTests COMMAND test_dxcluster)
