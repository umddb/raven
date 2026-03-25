find_package(Boost 1.88.0 EXACT REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(PostgreSQL REQUIRED)
find_package(fmt REQUIRED)

add_library(utils STATIC catcache.cpp config.cpp pgutil.cpp stringutil.cpp)

target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(utils PUBLIC ${ERCAT_THIRDPARTY_INCLUDE})

target_link_libraries(utils PRIVATE Boost::headers fmt::fmt)
target_link_libraries(utils PUBLIC PostgreSQL::PostgreSQL)
