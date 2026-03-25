#include "ERQLParser.h"

using namespace antlr4;

void ERQLParserBase::ParseRoutineBody()
{
}

bool ERQLParserBase::OnlyAcceptableOps()
{
	auto c = ((CommonTokenStream*)this->getInputStream())->LT(1);
	auto text = c->getText();
	return text == "!" || text == "!!"
			|| text == "!=-" // Code for specific example.
			;
}

