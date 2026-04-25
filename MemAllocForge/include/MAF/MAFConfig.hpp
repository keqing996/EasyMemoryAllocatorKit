#pragma once

#include <stdexcept>

#ifndef MAF_DEALLOCATE_STRICT
    #ifdef NDEBUG
        #define MAF_DEALLOCATE_STRICT 0
    #else
        #define MAF_DEALLOCATE_STRICT 1
    #endif
#endif

#if MAF_DEALLOCATE_STRICT
    #define MAF_DEALLOC_FAIL(msg) \
        throw std::invalid_argument(msg)
    #define MAF_DEALLOC_FAIL_VAL(msg, val) \
        throw std::invalid_argument(msg)
#else
    #define MAF_DEALLOC_FAIL(msg) \
        return
    #define MAF_DEALLOC_FAIL_VAL(msg, val) \
        return (val)
#endif
