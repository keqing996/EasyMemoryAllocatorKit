#pragma once

class Helper
{
public:
    Helper() = delete;

public:
    static size_t UpAlignment(size_t size, size_t alignment);
};