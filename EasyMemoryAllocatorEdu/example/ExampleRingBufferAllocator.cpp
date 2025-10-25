#include <cstdio>
#include <vector>
#include "EAllocKit/RingBufferAllocator.hpp"
#include "EAllocKit/STLAllocatorAdapter.hpp"

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
    printf("=== Network Packet Processing System with RingBufferAllocator ===\n");
    
    // Create a RingBufferAllocator for packet buffer (1MB ring buffer)
    EAllocKit::RingBufferAllocator packetBuffer(1024 * 1024);
    
    printf("Packet Buffer initialized: %.2f KB capacity\n", 
           packetBuffer.GetCapacity() / 1024.0f);
    printf("Processing incoming network packets in FIFO order...\n\n");
    
    // Packet structures
    struct PacketHeader {
        uint32_t packetId;
        uint16_t sourcePort;
        uint16_t destPort;
        uint32_t timestamp;
        uint16_t dataLength;
    };
    
    struct Packet {
        PacketHeader header;
        uint8_t* payload;
    };
    
    // Helper to allocate packet
    auto allocatePacket = [&](uint32_t id, uint16_t srcPort, uint16_t dstPort, 
                              uint16_t payloadSize) -> Packet* {
        // Allocate packet structure
        Packet* pkt = New<Packet>(packetBuffer);
        if (!pkt) return nullptr;
        
        pkt->header.packetId = id;
        pkt->header.sourcePort = srcPort;
        pkt->header.destPort = dstPort;
        pkt->header.timestamp = id * 100; // Simulated timestamp
        pkt->header.dataLength = payloadSize;
        
        // Allocate payload
        if (payloadSize > 0) {
            pkt->payload = static_cast<uint8_t*>(packetBuffer.Allocate(payloadSize));
            if (!pkt->payload) {
                return nullptr;
            }
        } else {
            pkt->payload = nullptr;
        }
        
        return pkt;
    };
    
    printf("--- Phase 1: Receiving Initial Packets ---\n");
    
    std::vector<Packet*> receivedPackets;
    
    // Receive small control packets
    for (int i = 1; i <= 5; ++i) {
        Packet* pkt = allocatePacket(i, 8080, 9090, 64);
        if (pkt) {
            receivedPackets.push_back(pkt);
            printf("Received packet #%d: %d bytes payload\n", i, pkt->header.dataLength);
        }
    }
    
    printf("Buffer used: %.2f KB / %.2f KB\n\n",
           packetBuffer.GetUsedSpace() / 1024.0f,
           packetBuffer.GetCapacity() / 1024.0f);
    
    printf("--- Phase 2: Processing and Releasing Packets (FIFO) ---\n");
    
    // Process first 3 packets in FIFO order
    for (int i = 0; i < 3; ++i) {
        if (receivedPackets[i]) {
            printf("Processing packet #%d (src:%d, dst:%d)\n",
                   receivedPackets[i]->header.packetId,
                   receivedPackets[i]->header.sourcePort,
                   receivedPackets[i]->header.destPort);
            
            // Deallocate in FIFO order (payload first, then packet)
            packetBuffer.DeallocateNext(); // Deallocate packet structure
            packetBuffer.DeallocateNext(); // Deallocate payload
        }
    }
    
    printf("3 packets processed and freed\n");
    printf("Buffer used: %.2f KB (space reclaimed at the front)\n\n",
           packetBuffer.GetUsedSpace() / 1024.0f);
    
    printf("--- Phase 3: Continuous Packet Stream ---\n");
    
    // Simulate continuous packet arrival and processing
    for (int cycle = 1; cycle <= 3; ++cycle) {
        printf("Cycle %d:\n", cycle);
        
        // Receive 4 new packets of varying sizes
        Packet* pkt1 = allocatePacket(100 + cycle * 10 + 1, 8080, 9090, 128);
        Packet* pkt2 = allocatePacket(100 + cycle * 10 + 2, 8080, 9090, 256);
        Packet* pkt3 = allocatePacket(100 + cycle * 10 + 3, 8080, 9090, 512);
        Packet* pkt4 = allocatePacket(100 + cycle * 10 + 4, 8080, 9090, 64);
        
        printf("  Received 4 packets (sizes: 128, 256, 512, 64 bytes)\n");
        printf("  Buffer used: %.2f KB\n", packetBuffer.GetUsedSpace() / 1024.0f);
        
        // Process 2 oldest packets
        packetBuffer.DeallocateNext();
        packetBuffer.DeallocateNext();
        packetBuffer.DeallocateNext();
        packetBuffer.DeallocateNext();
        
        printf("  Processed 2 packets\n");
        printf("  Buffer used: %.2f KB\n\n", packetBuffer.GetUsedSpace() / 1024.0f);
    }
    
    printf("--- Phase 4: Large Packet Handling ---\n");
    
    // Receive a large packet (e.g., file transfer)
    Packet* largePkt = allocatePacket(200, 21, 22, 32 * 1024); // 32KB payload
    if (largePkt) {
        printf("Received large packet #%d: %d KB payload\n",
               largePkt->header.packetId,
               largePkt->header.dataLength / 1024);
        printf("Buffer used: %.2f KB\n", packetBuffer.GetUsedSpace() / 1024.0f);
    }
    
    // Process some packets to make room
    printf("Processing packets to free space...\n");
    for (int i = 0; i < 10; ++i) {
        packetBuffer.DeallocateNext();
    }
    
    printf("Buffer used after processing: %.2f KB\n\n", packetBuffer.GetUsedSpace() / 1024.0f);
    
    printf("--- Phase 5: High Throughput Simulation ---\n");
    
    int packetsReceived = 0;
    int packetsProcessed = 0;
    
    // Simulate high-throughput scenario
    for (int batch = 1; batch <= 5; ++batch) {
        // Receive burst of 10 packets
        for (int i = 0; i < 10; ++i) {
            uint16_t size = 64 + (i % 4) * 64; // Varying sizes: 64, 128, 192, 256
            Packet* pkt = allocatePacket(300 + batch * 10 + i, 8080, 9090, size);
            if (pkt) {
                packetsReceived++;
            }
        }
        
        // Process 8 packets (slightly slower than receiving)
        for (int i = 0; i < 16; ++i) { // 8 packets × 2 deallocations each
            packetBuffer.DeallocateNext();
            packetsProcessed++;
        }
    }
    
    printf("Processed %d packets in high-throughput mode\n", packetsProcessed / 2);
    printf("Buffer used: %.2f KB\n\n", packetBuffer.GetUsedSpace() / 1024.0f);
    
    printf("--- Phase 6: Buffer Wrap-Around ---\n");
    
    // Fill buffer close to capacity
    printf("Filling buffer to near capacity...\n");
    int count = 0;
    while (true) {
        Packet* pkt = allocatePacket(400 + count, 8080, 9090, 1024);
        if (!pkt) break;
        count++;
    }
    
    printf("Allocated %d packets (1KB each)\n", count);
    printf("Buffer used: %.2f KB (%.1f%% full)\n",
           packetBuffer.GetUsedSpace() / 1024.0f,
           (packetBuffer.GetUsedSpace() * 100.0f) / packetBuffer.GetCapacity());
    
    // Process half of the packets to create space at the front
    printf("Processing half the packets...\n");
    for (int i = 0; i < count; ++i) {
        packetBuffer.DeallocateNext();
    }
    
    printf("Buffer used: %.2f KB\n", packetBuffer.GetUsedSpace() / 1024.0f);
    
    // Now allocate new packets - they will wrap around
    printf("Allocating new packets (will wrap around to the front)...\n");
    for (int i = 0; i < 5; ++i) {
        Packet* pkt = allocatePacket(500 + i, 8080, 9090, 2048);
        if (pkt) {
            printf("  Allocated packet #%d\n", 500 + i);
        }
    }
    
    printf("Buffer used: %.2f KB (ring buffer wrapped around)\n\n",
           packetBuffer.GetUsedSpace() / 1024.0f);
    
    printf("--- Phase 7: Using STL Containers ---\n");
    
    using IntAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::RingBufferAllocator>;
    std::vector<int, IntAllocator> packetIds{IntAllocator(&packetBuffer)};
    
    // Store some packet IDs
    for (int i = 600; i < 620; ++i) {
        packetIds.push_back(i);
    }
    
    printf("Stored %zu packet IDs in STL vector\n", packetIds.size());
    printf("Buffer used: %.2f KB\n\n", packetBuffer.GetUsedSpace() / 1024.0f);
    
    printf("--- Phase 8: Buffer Reset ---\n");
    
    printf("Resetting ring buffer (clearing all data)...\n");
    packetBuffer.Reset();
    
    printf("Buffer used: %.2f KB (all data cleared)\n", 
           packetBuffer.GetUsedSpace() / 1024.0f);
    printf("Buffer is empty, ready for new packets\n\n");
    
    printf("--- Phase 9: Fresh Packet Processing ---\n");
    
    // Start fresh packet processing
    for (int i = 1; i <= 10; ++i) {
        uint16_t size = i * 100;
        Packet* pkt = allocatePacket(700 + i, 3000 + i, 4000 + i, size);
        if (pkt) {
            printf("New packet #%d: %d bytes (port %d -> %d)\n",
                   pkt->header.packetId,
                   pkt->header.dataLength,
                   pkt->header.sourcePort,
                   pkt->header.destPort);
        }
    }
    
    printf("\nBuffer used: %.2f KB / %.2f KB\n",
           packetBuffer.GetUsedSpace() / 1024.0f,
           packetBuffer.GetCapacity() / 1024.0f);
    
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
