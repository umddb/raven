set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# debug
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

# release
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# thirdparty libraries
set(ERCAT_THIRDPARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/install)
set(ERCAT_THIRDPARTY_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/install/include)
set(ERCAT_THIRDPARTY_LIB ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/install/lib)

find_package(fmt REQUIRED)
find_package(OpenSSL REQUIRED)

# Note that order matters (in the order of dependencies)
add_subdirectory(utils)
add_subdirectory(catalog)
add_subdirectory(gc)

add_executable(testercat main.cpp)
add_executable(setup setup.cpp)
add_executable(server server.cpp)
add_executable(rungc rungc.cpp)

target_link_libraries(testercat PRIVATE utils gc parser translator service grpc fmt::fmt)

target_compile_options(testercat PRIVATE -Wall -Wextra)

target_link_libraries(setup PRIVATE utils gc parser translator service grpc)

target_compile_options(setup PRIVATE -Wall -Wextra)

target_link_libraries(server PRIVATE utils gc parser translator service grpc fmt::fmt)

target_compile_options(server PRIVATE -Wall -Wextra)

target_link_libraries(rungc PRIVATE utils gc parser translator service grpc fmt::fmt)

target_compile_options(rungc PRIVATE -Wall -Wextra)