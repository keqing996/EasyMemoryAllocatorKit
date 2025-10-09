#pragma once

#include <cstddef>
#include <memory>
#include <limits>
#include <type_traits>

namespace EAllocKit
{
    class PoolAllocator;

    // Trait to detect if an allocator is a PoolAllocator
    // -> Because PoolAllocator has different interface (Allocate() without size)
    template <typename Allocator>
    struct IsPoolAllocator : std::false_type {};

    template <>
    struct IsPoolAllocator<PoolAllocator> : std::true_type {};

    template <typename T, typename Allocator>
    class STLAllocatorAdapter
    {
    public:
        // STL allocator type definitions
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        // Rebind allocator to different type
        template <typename U>
        struct rebind
        {
            using other = STLAllocatorAdapter<U, Allocator>;
        };

        // Constructors
        explicit STLAllocatorAdapter(Allocator* allocator) noexcept
            : _pAllocator(allocator)
        {
        }

        template <typename U>
        STLAllocatorAdapter(const STLAllocatorAdapter<U, Allocator>& other) noexcept
            : _pAllocator(other._pAllocator)
        {
        }

        STLAllocatorAdapter(const STLAllocatorAdapter& other) noexcept = default;
        STLAllocatorAdapter& operator=(const STLAllocatorAdapter& other) noexcept = default;

        // Allocate memory for n objects of type T
        pointer allocate(size_type n)
        {
            if (n == 0)
                return nullptr;

            if (n > max_size())
                throw std::bad_alloc();

            void* p = nullptr;

            // PoolAllocator has different interface
            if constexpr (IsPoolAllocator<Allocator>::value)
            {
                // PoolAllocator can only allocate one object at a time
                if (n != 1)
                    throw std::bad_alloc();
                p = _pAllocator->Allocate();
            }
            else
            {
                // Regular allocators take size parameter
                p = _pAllocator->Allocate(n * sizeof(T));
            }

            if (!p)
                throw std::bad_alloc();

            return static_cast<pointer>(p);
        }

        // Deallocate memory
        void deallocate(pointer p, size_type n) noexcept
        {
            if (p)
                _pAllocator->Deallocate(p);
        }

        // Maximum number of elements that can be allocated
        size_type max_size() const noexcept
        {
            if constexpr (IsPoolAllocator<Allocator>::value)
                return 1; // PoolAllocator can only allocate one object at a time
            else
                return std::numeric_limits<size_type>::max() / sizeof(T);
        }

        // Construct an object at the given address
        template <typename U, typename... Args>
        void construct(U* p, Args&&... args)
        {
            ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
        }

        // Destroy an object at the given address
        template <typename U>
        void destroy(U* p)
        {
            p->~U();
        }

        // Get the address of an object
        pointer address(reference x) const noexcept
        {
            return std::addressof(x);
        }

        const_pointer address(const_reference x) const noexcept
        {
            return std::addressof(x);
        }

        // Comparison operators
        template <typename U>
        bool operator==(const STLAllocatorAdapter<U, Allocator>& other) const noexcept
        {
            return _pAllocator == other._pAllocator;
        }

        template <typename U>
        bool operator!=(const STLAllocatorAdapter<U, Allocator>& other) const noexcept
        {
            return _pAllocator != other._pAllocator;
        }

        // Allow other instantiations to access private members
        template <typename U, typename A>
        friend class STLAllocatorAdapter;

    private:
        Allocator* _pAllocator;
    };
}
