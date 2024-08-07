#include "Allocator/Helper.h"

size_t Helper::UpAlignment(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}
