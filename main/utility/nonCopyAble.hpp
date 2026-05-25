#pragma once
class NonCopyAble
{
public:
	NonCopyAble() = default;
	NonCopyAble(NonCopyAble&) = delete;
	NonCopyAble(NonCopyAble&&) = default;
};
