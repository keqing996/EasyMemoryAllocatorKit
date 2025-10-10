#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <new>
#include <stdexcept>
#include "Util/Util.hpp"

namespace EAllocKit
{
    template <size_t FLI = 25, size_t SLI = 4>
    class TLSFAllocator
    {
    private:
        // TLSF block header structure
        struct BlockHeader
        {
            // Size and status flags
            size_t size;              // Block size (in bytes)
            BlockHeader* prevPhysical; // Previous physical block
            BlockHeader* nextFree;     // Next free block in segregated list
            BlockHeader* prevFree;     // Previous free block in segregated list
            
            // Status flags stored in lower bits of size
            static constexpr size_t FREE_BIT = 1;
            static constexpr size_t PREV_FREE_BIT = 2;
            static constexpr size_t SIZE_MASK = ~(FREE_BIT | PREV_FREE_BIT);
            
            void SetSize(size_t s) { size = (size & ~SIZE_MASK) | (s & SIZE_MASK); }
            size_t GetSize() const { return size & SIZE_MASK; }
            
            void SetFree(bool free) 
            { 
                if (free) size |= FREE_BIT;
                else size &= ~FREE_BIT;
            }
            bool IsFree() const { return (size & FREE_BIT) != 0; }
            
            void SetPrevFree(bool free)
            {
                if (free) size |= PREV_FREE_BIT;
                else size &= ~PREV_FREE_BIT;
            }
            bool IsPrevFree() const { return (size & PREV_FREE_BIT) != 0; }
            
            BlockHeader* GetNextPhysical() const
            {
                return reinterpret_cast<BlockHeader*>(
                    reinterpret_cast<uint8_t*>(const_cast<BlockHeader*>(this)) + 
                    sizeof(BlockHeader) + GetSize()
                );
            }
            
            void* GetDataPtr()
            {
                return reinterpret_cast<void*>(
                    reinterpret_cast<uint8_t*>(this) + sizeof(BlockHeader)
                );
            }
            
            static BlockHeader* FromDataPtr(void* ptr)
            {
                return reinterpret_cast<BlockHeader*>(
                    reinterpret_cast<uint8_t*>(ptr) - sizeof(BlockHeader)
                );
            }
        };
        
        static constexpr size_t SLI_COUNT = (1 << SLI);
        static constexpr size_t MIN_BLOCK_SIZE = 32;  // Increased minimum block size
        static constexpr size_t SMALL_BLOCK_SIZE = 256;
        
        // Bitmap type selection based on FLI size
        using BitmapType = typename std::conditional<(FLI <= 32), uint32_t, uint64_t>::type;
        
    public:
        explicit TLSFAllocator(size_t size, size_t defaultAlignment = 8);
        ~TLSFAllocator();
        
        TLSFAllocator(const TLSFAllocator& rhs) = delete;
        TLSFAllocator(TLSFAllocator&& rhs) = delete;
        
    public:
        void* Allocate(size_t size);
        void* Allocate(size_t size, size_t alignment);
        void Deallocate(void* ptr);
        void* GetMemoryBlockPtr() const { return _pData; }
        size_t GetTotalSize() const { return _size; }
        
    private:
        // Mapping functions
        void MappingInsert(size_t size, int* fli, int* sli);
        void MappingSearch(size_t size, int* fli, int* sli);
        
        // Bitmap operations
        void SetBit(BitmapType& bitmap, int index) { bitmap |= (BitmapType(1) << index); }
        void ClearBit(BitmapType& bitmap, int index) { bitmap &= ~(BitmapType(1) << index); }
        bool TestBit(BitmapType bitmap, int index) const { return (bitmap & (BitmapType(1) << index)) != 0; }
        int FindFirstSet(BitmapType bitmap) const;
        
        // Block operations
        BlockHeader* FindSuitableBlock(int* fli, int* sli);
        void RemoveBlock(BlockHeader* block, int fli, int sli);
        void InsertBlock(BlockHeader* block, int fli, int sli);
        BlockHeader* SplitBlock(BlockHeader* block, size_t size);
        BlockHeader* CoalesceWithPrev(BlockHeader* block);
        BlockHeader* CoalesceWithNext(BlockHeader* block);
        void MarkBlockUsed(BlockHeader* block);
        void MarkBlockFree(BlockHeader* block);
        
        // Helper functions
        size_t AdjustRequestSize(size_t size, size_t alignment);
        bool IsAligned(void* ptr, size_t alignment) const;
        
    private:
        void* _pData;                                    // Memory pool
        size_t _size;                                    // Total size
        size_t _defaultAlignment;                        // Default alignment
        BitmapType _flBitmap;                            // First level bitmap
        BitmapType _slBitmap[FLI];                       // Second level bitmaps
        BlockHeader* _blocks[FLI][SLI_COUNT];            // Segregated free lists
    };
    
    template<size_t FLI, size_t SLI>
    TLSFAllocator<FLI, SLI>::TLSFAllocator(size_t size, size_t defaultAlignment)
        : _pData(nullptr)
        , _size(size)
        , _defaultAlignment(defaultAlignment)
        , _flBitmap(0)
    {
        if (!Util::IsPowerOfTwo(defaultAlignment))
            throw std::invalid_argument("TLSFAllocator defaultAlignment must be a power of 2");
            
        // Initialize bitmaps and block lists
        std::memset(_slBitmap, 0, sizeof(_slBitmap));
        std::memset(_blocks, 0, sizeof(_blocks));
        
        // Allocate memory pool
        _pData = ::malloc(_size);
        if (!_pData)
            throw std::bad_alloc();
        
        // Initialize first block to span entire pool
        BlockHeader* firstBlock = static_cast<BlockHeader*>(_pData);
        size_t blockSize = _size - sizeof(BlockHeader);
        
        firstBlock->SetSize(blockSize);
        firstBlock->SetFree(true);
        firstBlock->SetPrevFree(false);
        firstBlock->prevPhysical = nullptr;
        firstBlock->nextFree = nullptr;
        firstBlock->prevFree = nullptr;
        
        // Insert into appropriate segregated list
        int fli, sli;
        MappingInsert(blockSize, &fli, &sli);
        InsertBlock(firstBlock, fli, sli);
    }
    
    template<size_t FLI, size_t SLI>
    TLSFAllocator<FLI, SLI>::~TLSFAllocator()
    {
        if (_pData)
        {
            ::free(_pData);
            _pData = nullptr;
        }
    }
    
    template<size_t FLI, size_t SLI>
    void* TLSFAllocator<FLI, SLI>::Allocate(size_t size)
    {
        return Allocate(size, _defaultAlignment);
    }
    
    template<size_t FLI, size_t SLI>
    void* TLSFAllocator<FLI, SLI>::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;
            
        if (!Util::IsPowerOfTwo(alignment))
            throw std::invalid_argument("TLSFAllocator only supports power-of-2 alignments");
        
        // Adjust size for alignment and minimum requirements
        size_t adjustedSize = AdjustRequestSize(size, alignment);
        
        // Find suitable block
        int fli, sli;
        MappingSearch(adjustedSize, &fli, &sli);
        BlockHeader* block = FindSuitableBlock(&fli, &sli);
        
        if (!block)
            return nullptr; // Out of memory
        
        // Remove block from free list
        RemoveBlock(block, fli, sli);
        
        // Split block if too large
        size_t blockSize = block->GetSize();
        if (blockSize >= adjustedSize + sizeof(BlockHeader) + MIN_BLOCK_SIZE)
        {
            BlockHeader* remainder = SplitBlock(block, adjustedSize);
            
            // Insert remainder back into free list
            int remainderFli, remainderSli;
            MappingInsert(remainder->GetSize(), &remainderFli, &remainderSli);
            InsertBlock(remainder, remainderFli, remainderSli);
        }
        
        // Mark block as used
        MarkBlockUsed(block);
        
        return block->GetDataPtr();
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::Deallocate(void* ptr)
    {
        if (!ptr)
            return;
        
        BlockHeader* block = BlockHeader::FromDataPtr(ptr);
        
        if (block->IsFree())
            return; // Double free
        
        // Mark block as free
        MarkBlockFree(block);
        
        // Coalesce with adjacent free blocks
        if (block->IsPrevFree())
            block = CoalesceWithPrev(block);
        
        BlockHeader* nextBlock = block->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
        {
            if (nextBlock->IsFree())
                block = CoalesceWithNext(block);
        }
        
        // Insert back into appropriate free list
        int fli, sli;
        MappingInsert(block->GetSize(), &fli, &sli);
        InsertBlock(block, fli, sli);
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::MappingInsert(size_t size, int* fli, int* sli)
    {
        if (size < SMALL_BLOCK_SIZE)
        {
            // Small blocks: use linear subdivision
            *fli = 0;
            *sli = static_cast<int>(size) / (SMALL_BLOCK_SIZE / SLI_COUNT);
            if (*sli >= static_cast<int>(SLI_COUNT))
                *sli = SLI_COUNT - 1;
        }
        else
        {
            // Large blocks: use logarithmic first level, linear second level
            // Find the position of the most significant bit
            int msb = 0;
            size_t temp = size;
            while (temp >>= 1)
            {
                msb++;
            }
            
            // First level index is the MSB position
            *fli = msb;
            
            // Second level index: use the next SLI bits after the MSB
            // Extract SLI bits starting from position (msb - SLI)
            *sli = static_cast<int>((size >> (msb - SLI)) & (SLI_COUNT - 1));
            
            // Adjust fli to account for SMALL_BLOCK_SIZE threshold
            // SMALL_BLOCK_SIZE = 256 = 2^8, so we subtract 8 from fli
            int smallBlockMsb = 7; // log2(128) - we start from 128 to avoid overlap
            while ((1 << smallBlockMsb) < SMALL_BLOCK_SIZE)
                smallBlockMsb++;
            
            *fli = *fli - smallBlockMsb + 1;
            
            // Clamp to valid range
            if (*fli < 0)
            {
                *fli = 0;
                *sli = SLI_COUNT - 1;
            }
            else if (*fli >= static_cast<int>(FLI))
            {
                *fli = FLI - 1;
                *sli = SLI_COUNT - 1;
            }
            else if (*sli < 0)
            {
                *sli = 0;
            }
            else if (*sli >= static_cast<int>(SLI_COUNT))
            {
                *sli = SLI_COUNT - 1;
            }
        }
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::MappingSearch(size_t size, int* fli, int* sli)
    {
        // Round up to next size class for search
        MappingInsert(size, fli, sli);
        
        // Search in same second-level class or higher
        if (*sli < static_cast<int>(SLI_COUNT) - 1)
        {
            (*sli)++;
        }
        else
        {
            (*fli)++;
            *sli = 0;
            
            // Ensure we don't exceed bounds
            if (*fli >= static_cast<int>(FLI))
            {
                *fli = FLI - 1;
                *sli = SLI_COUNT - 1;
            }
        }
    }
    
    template<size_t FLI, size_t SLI>
    int TLSFAllocator<FLI, SLI>::FindFirstSet(BitmapType bitmap) const
    {
        if (bitmap == 0)
            return -1;
        
        int position = 0;
        
        if constexpr (sizeof(BitmapType) == 8)
        {
            if ((bitmap & 0xFFFFFFFF) == 0) { bitmap >>= 32; position += 32; }
        }
        if ((bitmap & 0xFFFF) == 0) { bitmap >>= 16; position += 16; }
        if ((bitmap & 0xFF) == 0) { bitmap >>= 8; position += 8; }
        if ((bitmap & 0xF) == 0) { bitmap >>= 4; position += 4; }
        if ((bitmap & 0x3) == 0) { bitmap >>= 2; position += 2; }
        if ((bitmap & 0x1) == 0) { position += 1; }
        
        return position;
    }
    
    template<size_t FLI, size_t SLI>
    typename TLSFAllocator<FLI, SLI>::BlockHeader* 
    TLSFAllocator<FLI, SLI>::FindSuitableBlock(int* fli, int* sli)
    {
        // Validate indices
        if (*fli < 0 || *fli >= static_cast<int>(FLI) || *sli < 0 || *sli >= static_cast<int>(SLI_COUNT))
            return nullptr;
        
        // Search in current second-level list
        BitmapType slMap = _slBitmap[*fli] & (~BitmapType(0) << *sli);
        
        if (slMap == 0)
        {
            // Search in higher first-level lists
            BitmapType flMap = _flBitmap & (~BitmapType(0) << (*fli + 1));
            if (flMap == 0)
                return nullptr; // No suitable block found
            
            *fli = FindFirstSet(flMap);
            if (*fli < 0 || *fli >= static_cast<int>(FLI))
                return nullptr;
                
            slMap = _slBitmap[*fli];
        }
        
        *sli = FindFirstSet(slMap);
        if (*sli < 0 || *sli >= static_cast<int>(SLI_COUNT))
            return nullptr;
            
        return _blocks[*fli][*sli];
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::RemoveBlock(BlockHeader* block, int fli, int sli)
    {
        // Validate indices
        if (fli < 0 || fli >= static_cast<int>(FLI) || sli < 0 || sli >= static_cast<int>(SLI_COUNT))
            return;
        
        BlockHeader* prev = block->prevFree;
        BlockHeader* next = block->nextFree;
        
        if (prev)
            prev->nextFree = next;
        if (next)
            next->prevFree = prev;
        
        // Update list head if necessary
        if (_blocks[fli][sli] == block)
        {
            _blocks[fli][sli] = next;
            
            // Update bitmaps if list is now empty
            if (!next)
            {
                ClearBit(_slBitmap[fli], sli);
                if (_slBitmap[fli] == 0)
                    ClearBit(_flBitmap, fli);
            }
        }
        
        block->nextFree = nullptr;
        block->prevFree = nullptr;
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::InsertBlock(BlockHeader* block, int fli, int sli)
    {
        // Validate indices
        if (fli < 0 || fli >= static_cast<int>(FLI) || sli < 0 || sli >= static_cast<int>(SLI_COUNT))
            return;
        
        BlockHeader* head = _blocks[fli][sli];
        
        block->nextFree = head;
        block->prevFree = nullptr;
        
        if (head)
            head->prevFree = block;
        
        _blocks[fli][sli] = block;
        
        // Update bitmaps
        SetBit(_flBitmap, fli);
        SetBit(_slBitmap[fli], sli);
    }
    
    template<size_t FLI, size_t SLI>
    typename TLSFAllocator<FLI, SLI>::BlockHeader* 
    TLSFAllocator<FLI, SLI>::SplitBlock(BlockHeader* block, size_t size)
    {
        BlockHeader* remainder = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader) + size
        );
        
        size_t remainderSize = block->GetSize() - size - sizeof(BlockHeader);
        
        remainder->SetSize(remainderSize);
        remainder->SetFree(true);
        remainder->SetPrevFree(false);
        remainder->prevPhysical = block;
        remainder->nextFree = nullptr;
        remainder->prevFree = nullptr;
        
        block->SetSize(size);
        
        // Update next physical block's prev pointer
        BlockHeader* nextBlock = remainder->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
        {
            nextBlock->prevPhysical = remainder;
            nextBlock->SetPrevFree(true);
        }
        
        return remainder;
    }
    
    template<size_t FLI, size_t SLI>
    typename TLSFAllocator<FLI, SLI>::BlockHeader* 
    TLSFAllocator<FLI, SLI>::CoalesceWithPrev(BlockHeader* block)
    {
        BlockHeader* prevBlock = block->prevPhysical;
        
        if (!prevBlock || !prevBlock->IsFree())
            return block;
        
        // Remove prev block from its free list
        int fli, sli;
        MappingInsert(prevBlock->GetSize(), &fli, &sli);
        RemoveBlock(prevBlock, fli, sli);
        
        // Merge blocks
        prevBlock->SetSize(prevBlock->GetSize() + sizeof(BlockHeader) + block->GetSize());
        
        // Update next block's prev pointer
        BlockHeader* nextBlock = prevBlock->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
            nextBlock->prevPhysical = prevBlock;
        
        return prevBlock;
    }
    
    template<size_t FLI, size_t SLI>
    typename TLSFAllocator<FLI, SLI>::BlockHeader* 
    TLSFAllocator<FLI, SLI>::CoalesceWithNext(BlockHeader* block)
    {
        BlockHeader* nextBlock = block->GetNextPhysical();
        
        if (!nextBlock->IsFree())
            return block;
        
        // Remove next block from its free list
        int fli, sli;
        MappingInsert(nextBlock->GetSize(), &fli, &sli);
        RemoveBlock(nextBlock, fli, sli);
        
        // Merge blocks
        block->SetSize(block->GetSize() + sizeof(BlockHeader) + nextBlock->GetSize());
        
        // Update next-next block's prev pointer
        BlockHeader* nextNextBlock = block->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextNextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
        {
            nextNextBlock->prevPhysical = block;
            nextNextBlock->SetPrevFree(true);
        }
        
        return block;
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::MarkBlockUsed(BlockHeader* block)
    {
        block->SetFree(false);
        
        // Update next block's prev-free flag
        BlockHeader* nextBlock = block->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
            nextBlock->SetPrevFree(false);
    }
    
    template<size_t FLI, size_t SLI>
    void TLSFAllocator<FLI, SLI>::MarkBlockFree(BlockHeader* block)
    {
        block->SetFree(true);
        
        // Update next block's prev-free flag
        BlockHeader* nextBlock = block->GetNextPhysical();
        if (reinterpret_cast<uint8_t*>(nextBlock) < reinterpret_cast<uint8_t*>(_pData) + _size)
            nextBlock->SetPrevFree(true);
    }
    
    template<size_t FLI, size_t SLI>
    size_t TLSFAllocator<FLI, SLI>::AdjustRequestSize(size_t size, size_t alignment)
    {
        // Align the size to the requested alignment
        size_t adjustedSize = Util::UpAlignment(size, alignment);
        
        // Also align to _defaultAlignment at minimum
        if (alignment < _defaultAlignment)
            adjustedSize = Util::UpAlignment(size, _defaultAlignment);
        
        // Ensure minimum block size
        if (adjustedSize < MIN_BLOCK_SIZE)
            adjustedSize = MIN_BLOCK_SIZE;
        
        return adjustedSize;
    }
    
    template<size_t FLI, size_t SLI>
    bool TLSFAllocator<FLI, SLI>::IsAligned(void* ptr, size_t alignment) const
    {
        return (reinterpret_cast<size_t>(ptr) & (alignment - 1)) == 0;
    }
}
