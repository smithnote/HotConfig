include(ExternalProject)
add_custom_target(deps)

set(DEPS_INSTALL_DIR "${PROJECT_SOURCE_DIR}/deps")
set(DEPS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/build")

ExternalProject_Add(
    dep_gtest
    GIT_REPOSITORY https://github.com/google/googletest
    PREFIX ${DEPS_SOURCE_DIR}/gtest
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_DIR}
               -DBUILD_SHARED_LIBS=OFF
    LOG_DOWNLOAD 1
    LOG_INSTALL 1
)

add_dependencies(deps dep_gtest)
install(DIRECTORY ${DEPS_INSTALL_DIR}/lib DESTINATION .)
