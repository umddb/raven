#pragma once

#include "antlr4-runtime.h"

class ERQLParserBase : public antlr4::Parser {
public:
    ERQLParserBase(antlr4::TokenStream *input) : Parser(input) { }
    void ParseRoutineBody();
    bool OnlyAcceptableOps();
};
