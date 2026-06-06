find_package(antlr4-runtime REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(antlr4-generator REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(Java REQUIRED COMPONENTS Runtime)

set(ANTLR_EXECUTABLE ${ERCAT_THIRDPARTY_PATH}/antlr-4.13.2-complete.jar)

# Define input grammar files
set(GRAMMAR_LEXER ${CMAKE_CURRENT_SOURCE_DIR}/ERQLLexer.g4)
set(GRAMMAR_PARSER ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParser.g4)

# Define expected outputs in the build directory
set(GENERATED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLLexer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLLexer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserBaseListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserBaseListener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserBaseVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserBaseVisitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserListener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ERQLParserVisitor.h
)

# Custom command to invoke ANTLR4 generator during the build
add_custom_command(
    OUTPUT ${GENERATED_SOURCES}
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR_EXECUTABLE} 
            -Dlanguage=Cpp -visitor -listener 
            -o ${CMAKE_CURRENT_SOURCE_DIR} 
            ${GRAMMAR_LEXER} ${GRAMMAR_PARSER}
    DEPENDS ${GRAMMAR_LEXER} ${GRAMMAR_PARSER}
    COMMENT "Generating ANTLR4 C++ sources for ERQL"
)

# Create the static library from the generated sources
add_library(parser STATIC ${GENERATED_SOURCES} ERQLLexerBase.cpp ERQLParserBase.cpp)

# Include the binary dir so the compiler can find the generated headers
target_include_directories(parser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${ERCAT_THIRDPARTY_INCLUDE}/antlr4-runtime
)

target_link_libraries(parser PUBLIC antlr4_static)