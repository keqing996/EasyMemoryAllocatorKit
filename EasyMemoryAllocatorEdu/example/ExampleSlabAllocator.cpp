#include <cstdio>
#include <vector>
#include "EAllocKit/SlabAllocator.hpp"

struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

template<typename T, typename Allocator>
T* New(Allocator& pAllocator);

template<typename T, typename... Args, typename Allocator>
T* New(Allocator& pAllocator, Args&&... args);

template<typename T, typename Allocator>
void Delete(Allocator& pAllocator, T* p);

int main()
{
    printf("=== Event System with SlabAllocator ===\n\n");
    
    // Event structures - all fixed size
    struct MouseEvent {
        int x, y;
        int button;
        bool pressed;
        uint64_t timestamp;
    };
    
    struct KeyboardEvent {
        int keyCode;
        bool pressed;
        bool shift, ctrl, alt;
        uint64_t timestamp;
    };
    
    struct NetworkEvent {
        int connectionId;
        int eventType;
        uint32_t dataSize;
        uint64_t timestamp;
    };
    
    // Create SlabAllocators for each event type
    EAllocKit::SlabAllocator mouseEventPool(sizeof(MouseEvent), 32);
    EAllocKit::SlabAllocator keyEventPool(sizeof(KeyboardEvent), 32);
    EAllocKit::SlabAllocator netEventPool(sizeof(NetworkEvent), 16);
    
    printf("Event pools initialized:\n");
    printf("  Mouse events: %zu bytes/object, %zu objects/slab\n",
           mouseEventPool.GetObjectSize(), mouseEventPool.GetObjectsPerSlab());
    printf("  Keyboard events: %zu bytes/object, %zu objects/slab\n",
           keyEventPool.GetObjectSize(), keyEventPool.GetObjectsPerSlab());
    printf("  Network events: %zu bytes/object, %zu objects/slab\n\n",
           netEventPool.GetObjectSize(), netEventPool.GetObjectsPerSlab());
    
    printf("--- Phase 1: Generating Mouse Events ---\n");
    
    std::vector<MouseEvent*> mouseEvents;
    
    // Simulate mouse movement and clicks
    for (int i = 0; i < 10; ++i) {
        MouseEvent* evt = New<MouseEvent>(mouseEventPool);
        if (evt) {
            evt->x = 100 + i * 50;
            evt->y = 200 + i * 30;
            evt->button = (i % 3); // 0=left, 1=right, 2=middle
            evt->pressed = (i % 2 == 0);
            evt->timestamp = 1000 + i * 100;
            mouseEvents.push_back(evt);
            
            printf("Mouse event: (%d, %d) button=%d %s\n",
                   evt->x, evt->y, evt->button,
                   evt->pressed ? "pressed" : "released");
        }
    }
    
    printf("Generated %zu mouse events\n", mouseEvents.size());
    printf("SlabAllocator stats - Total slabs: %zu, Active allocations: %zu\n\n",
           mouseEventPool.GetTotalSlabs(), mouseEventPool.GetTotalAllocations());
    
    printf("--- Phase 2: Processing Keyboard Events ---\n");
    
    std::vector<KeyboardEvent*> keyEvents;
    
    // Simulate keyboard input
    int keyCodes[] = {65, 83, 68, 70, 32, 13, 27, 16, 17, 18}; // A,S,D,F,Space,Enter,Esc,Shift,Ctrl,Alt
    
    for (int i = 0; i < 10; ++i) {
        KeyboardEvent* evt = New<KeyboardEvent>(keyEventPool);
        if (evt) {
            evt->keyCode = keyCodes[i];
            evt->pressed = (i % 2 == 0);
            evt->shift = (i >= 7);
            evt->ctrl = false;
            evt->alt = false;
            evt->timestamp = 2000 + i * 150;
            keyEvents.push_back(evt);
            
            printf("Key event: code=%d %s %s\n",
                   evt->keyCode,
                   evt->pressed ? "pressed" : "released",
                   evt->shift ? "[Shift]" : "");
        }
    }
    
    printf("Generated %zu keyboard events\n", keyEvents.size());
    printf("SlabAllocator stats - Total slabs: %zu, Active allocations: %zu\n\n",
           keyEventPool.GetTotalSlabs(), keyEventPool.GetTotalAllocations());
    
    printf("--- Phase 3: Network Events ---\n");
    
    std::vector<NetworkEvent*> netEvents;
    
    for (int i = 0; i < 5; ++i) {
        NetworkEvent* evt = New<NetworkEvent>(netEventPool);
        if (evt) {
            evt->connectionId = 1000 + i;
            evt->eventType = i % 3; // 0=connect, 1=data, 2=disconnect
            evt->dataSize = (i + 1) * 256;
            evt->timestamp = 3000 + i * 200;
            netEvents.push_back(evt);
            
            const char* typeNames[] = {"CONNECT", "DATA", "DISCONNECT"};
            printf("Network event: conn=%d type=%s size=%u\n",
                   evt->connectionId,
                   typeNames[evt->eventType],
                   evt->dataSize);
        }
    }
    
    printf("Generated %zu network events\n", netEvents.size());
    printf("SlabAllocator stats - Total slabs: %zu, Active allocations: %zu\n\n",
           netEventPool.GetTotalSlabs(), netEventPool.GetTotalAllocations());
    
    printf("--- Phase 4: Processing and Freeing Events ---\n");
    
    // Process and free mouse events
    printf("Processing mouse events...\n");
    for (MouseEvent* evt : mouseEvents) {
        Delete(mouseEventPool, evt);
    }
    mouseEvents.clear();
    printf("Mouse events freed. Active allocations: %zu\n\n",
           mouseEventPool.GetTotalAllocations());
    
    // Process and free keyboard events
    printf("Processing keyboard events...\n");
    for (KeyboardEvent* evt : keyEvents) {
        Delete(keyEventPool, evt);
    }
    keyEvents.clear();
    printf("Keyboard events freed. Active allocations: %zu\n\n",
           keyEventPool.GetTotalAllocations());
    
    printf("--- Phase 5: Event Burst (High Frequency) ---\n");
    
    // Simulate rapid mouse movement (e.g., dragging)
    printf("Simulating rapid mouse movement (50 events)...\n");
    std::vector<MouseEvent*> burstEvents;
    
    for (int i = 0; i < 50; ++i) {
        MouseEvent* evt = New<MouseEvent>(mouseEventPool);
        if (evt) {
            evt->x = 500 + i * 2;
            evt->y = 300 + (i % 20) * 5;
            evt->button = 0;
            evt->pressed = true;
            evt->timestamp = 5000 + i * 10;
            burstEvents.push_back(evt);
        }
    }
    
    printf("Generated %zu burst events\n", burstEvents.size());
    printf("SlabAllocator grew to %zu slabs to handle burst\n",
           mouseEventPool.GetTotalSlabs());
    printf("Active allocations: %zu\n\n", mouseEventPool.GetTotalAllocations());
    
    // Process burst events
    printf("Processing burst events...\n");
    for (MouseEvent* evt : burstEvents) {
        Delete(mouseEventPool, evt);
    }
    burstEvents.clear();
    printf("Burst events freed. Active allocations: %zu\n",
           mouseEventPool.GetTotalAllocations());
    printf("Slabs remain allocated for future reuse: %zu slabs\n\n",
           mouseEventPool.GetTotalSlabs());
    
    printf("--- Phase 6: Mixed Event Processing ---\n");
    
    // Simulate a game loop with mixed events
    printf("Game loop simulation (3 frames):\n");
    
    for (int frame = 1; frame <= 3; ++frame) {
        printf("Frame %d:\n", frame);
        
        // Generate various events this frame
        MouseEvent* m1 = New<MouseEvent>(mouseEventPool);
        MouseEvent* m2 = New<MouseEvent>(mouseEventPool);
        KeyboardEvent* k1 = New<KeyboardEvent>(keyEventPool);
        NetworkEvent* n1 = New<NetworkEvent>(netEventPool);
        
        if (m1) {
            m1->x = frame * 10;
            m1->y = frame * 20;
            m1->button = 0;
            m1->pressed = false;
            m1->timestamp = 6000 + frame * 16;
            printf("  Mouse move: (%d, %d)\n", m1->x, m1->y);
        }
        
        if (m2) {
            m2->x = frame * 15;
            m2->y = frame * 25;
            m2->button = 1;
            m2->pressed = true;
            m2->timestamp = 6000 + frame * 16 + 5;
            printf("  Mouse click: (%d, %d)\n", m2->x, m2->y);
        }
        
        if (k1) {
            k1->keyCode = 87; // W
            k1->pressed = true;
            k1->shift = false;
            k1->ctrl = false;
            k1->alt = false;
            k1->timestamp = 6000 + frame * 16 + 8;
            printf("  Key pressed: W\n");
        }
        
        if (n1) {
            n1->connectionId = 2000;
            n1->eventType = 1;
            n1->dataSize = 128;
            n1->timestamp = 6000 + frame * 16 + 12;
            printf("  Network data received\n");
        }
        
        // Process events immediately (typical for game loop)
        Delete(mouseEventPool, m1);
        Delete(mouseEventPool, m2);
        Delete(keyEventPool, k1);
        Delete(netEventPool, n1);
        
        printf("  Events processed and freed\n");
    }
    
    printf("\n");
    
    printf("--- Phase 7: Reusing Freed Objects ---\n");
    
    printf("Allocating new events (will reuse freed slab memory)...\n");
    
    for (int i = 0; i < 20; ++i) {
        MouseEvent* evt = New<MouseEvent>(mouseEventPool);
        if (evt) {
            evt->x = i * 10;
            evt->y = i * 10;
            evt->button = 0;
            evt->pressed = (i % 2 == 0);
            evt->timestamp = 7000 + i * 50;
        }
    }
    
    printf("Allocated 20 new mouse events\n");
    printf("Total slabs: %zu (no new slabs needed - reused existing)\n",
           mouseEventPool.GetTotalSlabs());
    printf("Active allocations: %zu\n\n", mouseEventPool.GetTotalAllocations());
    
    printf("--- Phase 8: Performance Test ---\n");
    
    printf("Rapid allocate/deallocate cycles (1000 iterations)...\n");
    
    for (int cycle = 0; cycle < 1000; ++cycle) {
        // Allocate
        KeyboardEvent* evt = New<KeyboardEvent>(keyEventPool);
        if (evt) {
            evt->keyCode = cycle % 256;
            evt->pressed = true;
            evt->shift = false;
            evt->ctrl = false;
            evt->alt = false;
            evt->timestamp = 8000 + cycle;
            
            // Immediately deallocate
            Delete(keyEventPool, evt);
        }
    }
    
    printf("Completed 1000 alloc/dealloc cycles\n");
    printf("Active allocations: %zu (should be 0)\n",
           keyEventPool.GetTotalAllocations());
    
    printf("--- Final Statistics ---\n");
    printf("Mouse Event Pool:\n");
    printf("  Total slabs: %zu\n", mouseEventPool.GetTotalSlabs());
    printf("  Active allocations: %zu\n", mouseEventPool.GetTotalAllocations());
    printf("  Capacity: %zu objects\n\n",
           mouseEventPool.GetTotalSlabs() * mouseEventPool.GetObjectsPerSlab());
    
    printf("Keyboard Event Pool:\n");
    printf("  Total slabs: %zu\n", keyEventPool.GetTotalSlabs());
    printf("  Active allocations: %zu\n", keyEventPool.GetTotalAllocations());
    printf("  Capacity: %zu objects\n\n",
           keyEventPool.GetTotalSlabs() * keyEventPool.GetObjectsPerSlab());
    
    printf("Network Event Pool:\n");
    printf("  Total slabs: %zu\n", netEventPool.GetTotalSlabs());
    printf("  Active allocations: %zu\n", netEventPool.GetTotalAllocations());
    printf("  Capacity: %zu objects\n\n",
           netEventPool.GetTotalSlabs() * netEventPool.GetObjectsPerSlab());
    
    return 0;
}

template<typename T, typename Allocator>
T* New(Allocator& pAllocator)
{
    void* pMem = pAllocator.Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T();
}

template<typename T, typename... Args, typename Allocator>
T* New(Allocator& pAllocator, Args&&... args)
{
    void* pMem = pAllocator.Allocate(sizeof(T));
    if (pMem == nullptr)
        return nullptr;
    return new (AllocatorMarker(), pMem) T(std::forward<Args>(args)...);
}

template<typename T, typename Allocator>
void Delete(Allocator& pAllocator, T* p)
{
    if (!p)
        return;
    p->~T();
    pAllocator.Deallocate(p);
}
