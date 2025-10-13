#pragma once

#include "LinearAllocator.hpp"
#include <array>
#include <memory>

namespace EAllocKit
{
    template<unsigned int N = 2>
    class FrameAllocator
    {
        static_assert(N >= 2, "FrameAllocator must have at least 2 buffers");

    public:
        explicit FrameAllocator(size_t frameSize, size_t defaultAlignment = 4);
        ~FrameAllocator();

        FrameAllocator(const FrameAllocator& rhs) = delete;
        FrameAllocator(FrameAllocator&& rhs) = delete;

    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* p);
        void SwapFrames();
        void Reset();
        void* GetCurrentFramePtr() const;
        void* GetPreviousFramePtr() const;
        void* GetFramePtr(unsigned int frameIndex) const;
        size_t GetCurrentFrameAvailableSpace() const;
        size_t GetPreviousFrameAvailableSpace() const;
        size_t GetFrameAvailableSpace(unsigned int frameIndex) const;
        size_t GetFrameSize() const;
        unsigned int GetCurrentFrameIndex() const;
        constexpr unsigned int GetBufferCount() const;

    private:
        std::array<std::unique_ptr<LinearAllocator>, N> _frames;
        unsigned int _currentFrameIndex;
        size_t _frameSize;
        size_t _defaultAlignment;
    };

    template<unsigned int N>
    inline FrameAllocator<N>::FrameAllocator(size_t frameSize, size_t defaultAlignment)
        : _currentFrameIndex(0)
        , _frameSize(frameSize)
        , _defaultAlignment(defaultAlignment)
    {
        // Initialize each LinearAllocator in the array
        for (unsigned int i = 0; i < N; ++i)
        {
            _frames[i] = std::make_unique<LinearAllocator>(frameSize, defaultAlignment);
        }
    }

    template<unsigned int N>
    inline FrameAllocator<N>::~FrameAllocator()
    {
        // LinearAllocator destructors handle cleanup automatically
    }

    template<unsigned int N>
    inline void* FrameAllocator<N>::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }

    template<unsigned int N>
    inline void* FrameAllocator<N>::Allocate(size_t size, size_t alignment)
    {
        return _frames[_currentFrameIndex]->Allocate(size, alignment);
    }

    template<unsigned int N>
    inline void FrameAllocator<N>::Deallocate(void* p)
    {
        // Frame allocators typically don't support individual deallocation
        // Use SwapFrames() to free the previous frame's memory
    }

    template<unsigned int N>
    inline void FrameAllocator<N>::SwapFrames()
    {
        // Switch to the next frame (circular)
        _currentFrameIndex = (_currentFrameIndex + 1) % N;
        
        // Reset the new current frame
        _frames[_currentFrameIndex]->Reset();
    }

    template<unsigned int N>
    inline void FrameAllocator<N>::Reset()
    {
        for (unsigned int i = 0; i < N; ++i)
        {
            _frames[i]->Reset();
        }
        _currentFrameIndex = 0;
    }

    template<unsigned int N>
    inline void* FrameAllocator<N>::GetCurrentFramePtr() const
    {
        return _frames[_currentFrameIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline void* FrameAllocator<N>::GetPreviousFramePtr() const
    {
        unsigned int previousIndex = (_currentFrameIndex + N - 1) % N;
        return _frames[previousIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline void* FrameAllocator<N>::GetFramePtr(unsigned int frameIndex) const
    {
        if (frameIndex >= N)
            return nullptr;
        return _frames[frameIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline size_t FrameAllocator<N>::GetCurrentFrameAvailableSpace() const
    {
        return _frames[_currentFrameIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline size_t FrameAllocator<N>::GetPreviousFrameAvailableSpace() const
    {
        unsigned int previousIndex = (_currentFrameIndex + N - 1) % N;
        return _frames[previousIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline size_t FrameAllocator<N>::GetFrameAvailableSpace(unsigned int frameIndex) const
    {
        if (frameIndex >= N)
            return 0;
        return _frames[frameIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline size_t FrameAllocator<N>::GetFrameSize() const
    {
        return _frameSize;
    }

    template<unsigned int N>
    inline unsigned int FrameAllocator<N>::GetCurrentFrameIndex() const
    {
        return _currentFrameIndex;
    }

    template<unsigned int N>
    constexpr inline unsigned int FrameAllocator<N>::GetBufferCount() const
    {
        return N;
    }

    // Type alias for backwards compatibility
    using DoubleBufferedFrameAllocator = FrameAllocator<2>;
}