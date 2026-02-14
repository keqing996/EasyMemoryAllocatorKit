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
        auto Allocate(size_t size) -> void*;
        auto Allocate(size_t size, size_t alignment) -> void*;
        auto Deallocate(void* p) -> void;
        auto SwapFrames() -> void;
        auto Reset() -> void;
        auto GetCurrentFramePtr() const -> void*;
        auto GetPreviousFramePtr() const -> void*;
        auto GetFramePtr(unsigned int frameIndex) const -> void*;
        auto GetCurrentFrameAvailableSpace() const -> size_t;
        auto GetPreviousFrameAvailableSpace() const -> size_t;
        auto GetFrameAvailableSpace(unsigned int frameIndex) const -> size_t;
        auto GetFrameSize() const -> size_t;
        auto GetCurrentFrameIndex() const -> unsigned int;
        constexpr auto GetBufferCount() const -> unsigned int;

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
    inline auto FrameAllocator<N>::Allocate(size_t size) -> void*
    {
        return Allocate(size, _defaultAlignment);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Allocate(size_t size, size_t alignment) -> void*
    {
        return _frames[_currentFrameIndex]->Allocate(size, alignment);
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Deallocate(void* p) -> void
    {
        // Frame allocators typically don't support individual deallocation
        // Use SwapFrames() to free the previous frame's memory
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::SwapFrames() -> void
    {
        // Switch to the next frame (circular)
        _currentFrameIndex = (_currentFrameIndex + 1) % N;
        
        // Reset the new current frame
        _frames[_currentFrameIndex]->Reset();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::Reset() -> void
    {
        for (unsigned int i = 0; i < N; ++i)
        {
            _frames[i]->Reset();
        }
        _currentFrameIndex = 0;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFramePtr() const -> void*
    {
        return _frames[_currentFrameIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetPreviousFramePtr() const -> void*
    {
        unsigned int previousIndex = (_currentFrameIndex + N - 1) % N;
        return _frames[previousIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFramePtr(unsigned int frameIndex) const -> void*
    {
        if (frameIndex >= N)
            return nullptr;
        return _frames[frameIndex]->GetMemoryBlockPtr();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFrameAvailableSpace() const -> size_t
    {
        return _frames[_currentFrameIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetPreviousFrameAvailableSpace() const -> size_t
    {
        unsigned int previousIndex = (_currentFrameIndex + N - 1) % N;
        return _frames[previousIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFrameAvailableSpace(unsigned int frameIndex) const -> size_t
    {
        if (frameIndex >= N)
            return 0;
        return _frames[frameIndex]->GetAvailableSpaceSize();
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetFrameSize() const -> size_t
    {
        return _frameSize;
    }

    template<unsigned int N>
    inline auto FrameAllocator<N>::GetCurrentFrameIndex() const -> unsigned int
    {
        return _currentFrameIndex;
    }

    template<unsigned int N>
    constexpr inline auto FrameAllocator<N>::GetBufferCount() const -> unsigned int
    {
        return N;
    }

    // Type alias for backwards compatibility
    using DoubleBufferedFrameAllocator = FrameAllocator<2>;
}