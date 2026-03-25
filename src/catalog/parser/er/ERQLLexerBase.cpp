#include "antlr4-runtime.h"
#include "ERQLLexerBase.h"
#include "ERQLLexer.h"
#include <locale>
#include <codecvt>
#include <cwctype>

ERQLLexerBase::ERQLLexerBase(antlr4::CharStream * input) : antlr4::Lexer(input)
{
    _input = input;
}

void ERQLLexerBase::PushTag()
{
	tags.push(this->getText());
}

bool ERQLLexerBase::IsTag()
{
	return this->getText() == tags.top();
}

void ERQLLexerBase::PopTag()
{
	tags.pop();
}

void ERQLLexerBase::UnterminatedBlockCommentDebugAssert()
{
}

bool ERQLLexerBase::CheckLaMinus()
{
	return this->getInputStream()->LA(1) != '-';
}

bool ERQLLexerBase::CheckLaStar()
{
	return this->getInputStream()->LA(1) != '*';
}

bool ERQLLexerBase::CharIsLetter()
{
	return std::iswalpha(static_cast<char>(this->getInputStream()->LA(-1)));
}

void ERQLLexerBase::HandleNumericFail()
{
	this->getInputStream()->seek(this->getInputStream()->index() - 2);
	this->setType(ERQLLexer::Integral);
}

void ERQLLexerBase::HandleLessLessGreaterGreater()
{
	if (this->getText() == "<<") this->setType(ERQLLexer::LESS_LESS);
	if (this->getText() == ">>") this->setType(ERQLLexer::GREATER_GREATER);
}


char32_t surrogate_to_utf32(char16_t high, char16_t low)
{ 
	return (high << 10) + low - 0x35fdc00; 
}

int toCodePoint(int high, int low)
{
	return surrogate_to_utf32(high, low);
}


bool ERQLLexerBase::CheckIfUtf32Letter()
{
	char high = static_cast<char>(this->getInputStream()->LA(-2));
	char low = static_cast<char>(this->getInputStream()->LA(-1));
	return std::iswalpha(toCodePoint(high, low));
}

bool ERQLLexerBase::IsSemiColon()
{
	return  ';' == (char)this->getInputStream()->LA(1);
}
