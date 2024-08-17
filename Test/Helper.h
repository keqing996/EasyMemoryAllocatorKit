#pragma once

#include <cstdio>

#define ToAddr(x) reinterpret_cast<size_t>(x)

template <typename T>
void PrintPtrAddr(const char* str, T* ptr)
{
    ::printf("%s", str);
    ::printf(" %llx\n", ToAddr(ptr));
}