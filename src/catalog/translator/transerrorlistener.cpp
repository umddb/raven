#include <sstream>

#include "catalog/translator/transerrorlistener.h"

namespace ercat {

TransErrorListener::TransErrorListener(std::vector<std::string>& errors) : errors_(errors) { }

TransErrorListener::~TransErrorListener() { }

void TransErrorListener::syntaxError(antlr4::Recognizer *recognizer, antlr4::Token * offendingSymbol, size_t line, 
        size_t charPositionInLine, const std::string &msg, std::exception_ptr e) {
    // add error messages to error vector and output to std::cerr
    std::ostringstream error_ss;
    error_ss << "line " << line << ":" << charPositionInLine << " " << msg;
    errors_.push_back(error_ss.str());
}    

}