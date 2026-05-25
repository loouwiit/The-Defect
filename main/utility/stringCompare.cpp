#include "stringCompare.hpp"

bool prefixCompare(const char* string, size_t stringLength, const char* prefix, size_t prefixLength)
{
	if (stringLength < prefixLength) return false; // 确保string的长度小等于prefix的长度
	size_t i = 0;
	while (i < prefixLength)
	{
		if (string[i] == prefix[i]) i++;
		else return false;
	}
	return true;
}

bool stringCompare(const char* string, size_t stringLength, const char* standard, size_t standardLength)
{
	if (stringLength != standardLength) return false;
	size_t i = 0;
	while (i < stringLength)
	{
		if (string[i] == standard[i]) i++;
		else return false;
	}
	return true;
}