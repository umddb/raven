find_package(antlr4-runtime REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(antlr4-generator REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})

set(ANTLR_EXECUTABLE ${ERCAT_THIRDPARTY_PATH}/antlr-4.13.2-complete.jar)

# TODO fix this so we generate grammar at runtime...
# java -jar ../../../thirdparty/install/antlr-4.13.2-complete.jar -Dlanguage=Cpp -visitor -listener ERQLLexer.g4 ERQLParser.g4

add_library(parser STATIC ERQLLexer.cpp ERQLLexerBase.cpp ERQLParser.cpp ERQLParserBase.cpp ERQLParserBaseListener.cpp ERQLParserBaseVisitor.cpp ERQLParserListener.cpp ERQLParserVisitor.cpp)

target_include_directories(parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${ERCAT_THIRDPARTY_INCLUDE}/antlr4-runtime)
target_link_libraries(parser PUBLIC antlr4_static)