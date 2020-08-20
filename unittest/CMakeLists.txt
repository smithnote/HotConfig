include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/deps/include)
link_directories(${PROJECT_SOURCE_DIR}/deps/lib)

add_executable(hot_config_unittest hot_config_unittest.cpp)
add_dependencies(hot_config_unittest deps)
target_link_libraries(hot_config_unittest pthread gtest)
install(TARGETS hot_config_unittest DESTINATION bin)
add_test(NAME unittest_hot_config COMMAND hot_config_unittest
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
