
set(CMAKE_PREFIX_PATH ${ERCAT_THIRDPARTY_PATH})
find_package(AWSSDK REQUIRED COMPONENTS s3 PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(Boost 1.88.0 EXACT REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(PostgreSQL REQUIRED)
find_package(TBB REQUIRED)
find_package(fmt REQUIRED)

add_library(gc STATIC filemanager.cpp gcgraph.cpp gcmanager.cpp versionchain.cpp versionmap.cpp)

target_include_directories(gc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(gc PUBLIC ${ERCAT_THIRDPARTY_INCLUDE})

target_link_libraries(gc PRIVATE ${AWSSDK_LINK_LIBRARIES})
target_link_libraries(gc PRIVATE Boost::headers)
find_library(ROCKSDB_LIBRARIES rocksdb PATHS ${ERCAT_THIRDPARTY_LIB})
target_link_libraries(gc PRIVATE ${ROCKSDB_LIBRARIES} pthread snappy bz2 lz4 zstd numa z uring fmt::fmt)
target_link_libraries(gc PRIVATE PostgreSQL::PostgreSQL)
target_link_libraries(gc PRIVATE TBB::tbb)

target_link_libraries(gc PRIVATE utils)