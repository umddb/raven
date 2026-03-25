find_package(antlr4-runtime REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(antlr4-generator REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(Boost 1.88.0 EXACT REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(fmt REQUIRED)

add_library(translator STATIC joingen.cpp transcontext.cpp transerrorlistener.cpp translator.cpp transvisitor.cpp)

target_include_directories(translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${ANTLR4_INCLUDE_DIR})
target_link_libraries(translator PRIVATE antlr4_static Boost::headers fmt::fmt parser)
target_link_libraries(translator PUBLIC utils)
