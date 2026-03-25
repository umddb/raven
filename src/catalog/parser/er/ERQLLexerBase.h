#pragma once
#include "antlr4-runtime.h"
#include <string>
#include <stack>

class ERQLLexerBase : public antlr4::Lexer
{
    public:
        ERQLLexerBase(antlr4::CharStream * input);
	void PushTag();
	bool IsTag();
	void PopTag();
	void UnterminatedBlockCommentDebugAssert();
	bool CheckLaMinus();
	bool CheckLaStar();
	bool CharIsLetter();
	void HandleNumericFail();
	void HandleLessLessGreaterGreater();
	bool CheckIfUtf32Letter();
	bool IsSemiColon();
    private:
        std::stack<std::string> tags;

};
