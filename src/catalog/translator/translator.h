#pragma once

#include "catalog/parser/er/ERQLLexer.h"
#include "catalog/parser/er/ERQLParser.h"
#include "catalog/translator/transcontext.h"
#include "catalog/translator/transerrorlistener.h"
#include "utils/catcache.h"

namespace ercat {

class Translator {
public:
    Translator();
    ~Translator();
    std::string translate(const std::string& input_command);
    std::string translateMultiDMLStmts(const std::string& input_command);
    const std::vector<std::string>& errors() const;

private:
    antlr4::ANTLRInputStream input_stream_;
    ERQLLexer erql_lexer_;
    antlr4::CommonTokenStream token_stream_;
    ERQLParser erql_parser_;
    std::vector<std::string> errors_;
    TransErrorListener error_listener_;
};

}