find_package(Boost 1.88.0 EXACT REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(PostgreSQL REQUIRED)
find_package(fmt REQUIRED)

add_library(service STATIC catalogservice.cpp txnmgr.cpp)

# ADD THIS: Force 'service' to wait for the protoc generation to finish
add_dependencies(service catalog_proto_gen)

target_include_directories(service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_include_directories(service PUBLIC ${ERCAT_THIRDPARTY_INCLUDE})

target_link_libraries(service PRIVATE Boost::headers pthread fmt::fmt PostgreSQL::PostgreSQL)
target_link_libraries(service PRIVATE utils translator)