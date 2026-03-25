#include <sstream>

#include "catalog/translator/translator.h"
#include "catalog/translator/transvisitor.h"

namespace ercat{

Translator::Translator() : input_stream_(), erql_lexer_(&input_stream_),
        token_stream_(&erql_lexer_), erql_parser_(&token_stream_), errors_(), error_listener_(errors_) { 
    erql_lexer_.removeErrorListeners();
    erql_parser_.removeErrorListeners();
    erql_lexer_.addErrorListener(&error_listener_);
    erql_parser_.addErrorListener(&error_listener_);
}

Translator::~Translator() { }

std::string Translator::translate(const std::string& input_command) {
    // clear out the error messages
    errors_.clear();
    // load the input string stream
    input_stream_.load(input_command);
    // set the lexer
    erql_lexer_.setInputStream(&input_stream_);
    // set the token stream
    token_stream_.setTokenSource(&erql_lexer_);
    // run the lexer
    token_stream_.fill();
    
    // if there are errors, return empty string
    if (!errors_.empty()) {
        return "";
    }

    // pipe tokens to parser
    erql_parser_.setTokenStream(&token_stream_);
    // build the parse tree
    ERQLParser::RootContext * parse_tree = erql_parser_.root();

    // if there are errors, return empty string
    if (!errors_.empty()) {
        return "";
    }

    // Construct translation context tree
    std::unique_ptr<TransVisitor> trans_visitor = std::make_unique<TransVisitor>(errors_);
    trans_visitor->visit(parse_tree);

    // if there are errors, return empty string
    if (!errors_.empty()) {
        return "";
    }

    std::vector<std::string> output_stream;
    output_stream.emplace_back();
    output_stream[0].reserve(input_command.size());
    // shared lock on cat_cache throughout translation of a single input for consistency
    std::shared_lock<std::shared_mutex> lock(CatCache::mtx());
    trans_visitor->rootTransContext()->translate(input_command, output_stream, errors_);
    lock.unlock();
    // if no errors, return the translated text
    if (errors_.empty()) {
        return output_stream[0];
    }

    return "";
}

std::string Translator::translateMultiDMLStmts(const std::string& input_command) {
    std::string input_stmt;
    std::string output_command;
    std::istringstream string_stream(input_command);
    errors_.clear();
    output_command.reserve(input_command.size()*2);
    while (std::getline(string_stream, input_stmt, ';') && errors_.empty()) {
        output_command.append(translate(input_stmt));
        output_command.append(";");
    }

    return output_command;
}

const std::vector<std::string>& Translator::errors() const {
    return errors_;
}

}