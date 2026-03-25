#pragma once

#include <vector>

#include "antlr4-runtime/antlr4-runtime.h"

namespace ercat {

class TransErrorListener : public antlr4::BaseErrorListener {
public:
    TransErrorListener(std::vector<std::string>& errors);
    ~TransErrorListener();
    void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token * offendingSymbol, size_t line, 
            size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override;    

private:
    std::vector<std::string>& errors_;
};
   
}