#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include "EAllocKit/PoolAllocator.hpp"

struct AllocatorMarker {};
inline void* operator new(size_t, AllocatorMarker, void* ptr) { return ptr; }
inline void operator delete(void*, AllocatorMarker, void*) { }

template<typename T, typename Allocator>
T* New(Allocator& pAllocator);

template<typename T, typename... Args, typename Allocator>
T* New(Allocator& pAllocator, Args&&... args);

template<typename T, typename Allocator>
void Delete(Allocator& pAllocator, T* p);

void DemonstrateBasicPoolUsage()
{
    printf("=== Pool Allocator Basic Usage Demo ===\n");
    
    // Create a pool for 64-byte blocks, 10 blocks total
    EAllocKit::PoolAllocator pool(64, 10);
    
    printf("Created PoolAllocator: 64-byte blocks, 10 blocks total\n");
    printf("Available blocks: %zu\n", pool.GetAvailableBlockCount());
    printf("Free list head: %p\n", pool.GetFreeListHeadNode());
    
    // Allocate some blocks
    void* ptr1 = pool.Allocate();
    void* ptr2 = pool.Allocate();
    void* ptr3 = pool.Allocate();
    
    printf("\nAllocated 3 blocks:\n");
    printf("  Block 1: %p\n", ptr1);
    printf("  Block 2: %p\n", ptr2);
    printf("  Block 3: %p\n", ptr3);
    printf("Available blocks after allocation: %zu\n", pool.GetAvailableBlockCount());
    
    // Deallocate one block
    printf("\nDeallocating block 2...\n");
    pool.Deallocate(ptr2);
    printf("Available blocks after deallocation: %zu\n", pool.GetAvailableBlockCount());
    
    // Allocate again - should reuse the freed block
    void* ptr4 = pool.Allocate();
    printf("\nAllocated new block: %p (should reuse freed block)\n", ptr4);
    printf("Available blocks: %zu\n", pool.GetAvailableBlockCount());
    
    // Clean up
    pool.Deallocate(ptr1);
    pool.Deallocate(ptr3);
    pool.Deallocate(ptr4);
    printf("\nAfter cleanup, available blocks: %zu\n", pool.GetAvailableBlockCount());
}

void DemonstrateObjectPool()
{
    printf("\n=== Pool Allocator Network Message Pool Demo ===\n");
    
    // Define a network message structure - perfect for pool allocation
    struct NetworkMessage
    {
        enum MessageType { CONNECT = 1, DISCONNECT = 2, DATA = 3, HEARTBEAT = 4 };
        
        MessageType type;
        uint32_t sessionId;
        uint32_t sequenceNum;
        uint16_t dataLength;
        uint8_t data[32];       // Fixed-size payload
        uint32_t timestamp;
        uint32_t checksum;
        
        void Initialize(MessageType msgType, uint32_t session, const char* payload = nullptr)
        {
            type = msgType;
            sessionId = session;
            sequenceNum = rand() % 10000;
            timestamp = static_cast<uint32_t>(time(nullptr));
            
            if (payload && msgType == DATA) {
                dataLength = static_cast<uint16_t>(strlen(payload));
                if (dataLength > sizeof(data)) dataLength = sizeof(data);
                memcpy(data, payload, dataLength);
            } else {
                dataLength = 0;
                memset(data, 0, sizeof(data));
            }
            
            // Simple checksum
            checksum = sessionId + sequenceNum + timestamp + dataLength;
        }
        
        const char* GetTypeName() const
        {
            switch (type) {
                case CONNECT: return "CONNECT";
                case DISCONNECT: return "DISCONNECT";
                case DATA: return "DATA";
                case HEARTBEAT: return "HEARTBEAT";
                default: return "UNKNOWN";
            }
        }
        
        void PrintInfo() const
        {
            printf("    %s [Session:%u, Seq:%u, Len:%u, Time:%u, Checksum:%u]\n",
                   GetTypeName(), sessionId, sequenceNum, dataLength, timestamp, checksum);
            if (type == DATA && dataLength > 0) {
                printf("      Payload: \"%.32s\"\n", reinterpret_cast<const char*>(data));
            }
        }
    };
    
    // Create pool for NetworkMessage objects
    EAllocKit::PoolAllocator messagePool(sizeof(NetworkMessage), 10);
    
    printf("Created network message pool: %zu-byte blocks, 10 messages max\n", sizeof(NetworkMessage));
    printf("Initial available blocks: %zu\n", messagePool.GetAvailableBlockCount());
    
    // Simulate incoming messages
    std::vector<NetworkMessage*> activeMessages;
    
    printf("\n--- Receiving network messages ---\n");
    
    // Connection messages
    NetworkMessage* connectMsg = static_cast<NetworkMessage*>(messagePool.Allocate());
    if (connectMsg) {
        connectMsg->Initialize(NetworkMessage::CONNECT, 1001);
        activeMessages.push_back(connectMsg);
        printf("Received CONNECT message at %p\n", connectMsg);
    }
    
    // Data messages
    NetworkMessage* dataMsg1 = static_cast<NetworkMessage*>(messagePool.Allocate());
    if (dataMsg1) {
        dataMsg1->Initialize(NetworkMessage::DATA, 1001, "Hello Server!");
        activeMessages.push_back(dataMsg1);
        printf("Received DATA message at %p\n", dataMsg1);
    }
    
    NetworkMessage* dataMsg2 = static_cast<NetworkMessage*>(messagePool.Allocate());
    if (dataMsg2) {
        dataMsg2->Initialize(NetworkMessage::DATA, 1001, "How are you?");
        activeMessages.push_back(dataMsg2);
        printf("Received DATA message at %p\n", dataMsg2);
    }
    
    // Heartbeat message
    NetworkMessage* heartbeatMsg = static_cast<NetworkMessage*>(messagePool.Allocate());
    if (heartbeatMsg) {
        heartbeatMsg->Initialize(NetworkMessage::HEARTBEAT, 1001);
        activeMessages.push_back(heartbeatMsg);
        printf("Received HEARTBEAT message at %p\n", heartbeatMsg);
    }
    
    printf("Available blocks after receiving messages: %zu\n", messagePool.GetAvailableBlockCount());
    
    // Display all messages
    printf("\n--- Message queue contents ---\n");
    for (const auto& message : activeMessages) {
        message->PrintInfo();
    }
    
    // Process and release some messages (simulate message processing)
    printf("\n--- Processing messages (releasing heartbeat and first data) ---\n");
    messagePool.Deallocate(activeMessages[1]);  // First data message
    messagePool.Deallocate(activeMessages[3]);  // Heartbeat message
    activeMessages[1] = nullptr;
    activeMessages[3] = nullptr;
    
    printf("Available blocks after processing: %zu\n", messagePool.GetAvailableBlockCount());
    
    // Receive new messages (should reuse freed blocks)
    printf("\n--- Receiving new messages (reusing freed blocks) ---\n");
    NetworkMessage* newDataMsg = static_cast<NetworkMessage*>(messagePool.Allocate());
    NetworkMessage* disconnectMsg = static_cast<NetworkMessage*>(messagePool.Allocate());
    
    if (newDataMsg) {
        newDataMsg->Initialize(NetworkMessage::DATA, 1001, "Goodbye!");
        printf("Received new DATA message at %p (reused block)\n", newDataMsg);
        activeMessages.push_back(newDataMsg);
    }
    
    if (disconnectMsg) {
        disconnectMsg->Initialize(NetworkMessage::DISCONNECT, 1001);
        printf("Received DISCONNECT message at %p (reused block)\n", disconnectMsg);
        activeMessages.push_back(disconnectMsg);
    }
    
    printf("Available blocks: %zu\n", messagePool.GetAvailableBlockCount());
    
    // Display final message queue
    printf("\n--- Final message queue ---\n");
    for (const auto& message : activeMessages) {
        if (message != nullptr) {
            message->PrintInfo();
        }
    }
    
    // Cleanup all messages
    for (auto& message : activeMessages) {
        if (message != nullptr) {
            messagePool.Deallocate(message);
        }
    }
}

void DemonstrateFixedSizeAdvantage()
{
    printf("\n=== Pool Allocator Fixed Size Advantage Demo ===\n");
    
    // Pool for exactly the size we need
    struct DataPacket
    {
        int type;
        float timestamp;
        char payload[24];
        int checksum;
    };
    
    EAllocKit::PoolAllocator packetPool(sizeof(DataPacket), 6);
    
    printf("\nDataPacket size: %zu bytes\n", sizeof(DataPacket));
    printf("Pool configured for %zu-byte blocks, 6 blocks\n", sizeof(DataPacket));
    printf("Available blocks: %zu\n", packetPool.GetAvailableBlockCount());
    
    printf("\n--- Rapid allocation/deallocation cycle ---\n");
    
    // Simulate rapid packet processing
    for (int cycle = 0; cycle < 3; ++cycle) {
        printf("\nCycle %d:\n", cycle + 1);
        
        std::vector<DataPacket*> packets;
        
        // Allocate several packets
        for (int i = 0; i < 4; ++i) {
            DataPacket* packet = static_cast<DataPacket*>(packetPool.Allocate());
            if (packet) {
                packet->type = i + 1;
                packet->timestamp = static_cast<float>(cycle * 100 + i);
                snprintf(packet->payload, sizeof(packet->payload), "Data%d_%d", cycle, i);
                packet->checksum = packet->type + static_cast<int>(packet->timestamp);
                packets.push_back(packet);
                printf("  Allocated packet %d (type=%d, time=%.1f)\n", 
                       i + 1, packet->type, packet->timestamp);
            }
        }
        
        printf("  Available blocks: %zu\n", packetPool.GetAvailableBlockCount());
        
        // Process and free packets
        for (auto* packet : packets) {
            printf("  Processing: %s (checksum=%d)\n", packet->payload, packet->checksum);
            packetPool.Deallocate(packet);
        }
        
        printf("  After processing - Available blocks: %zu\n", packetPool.GetAvailableBlockCount());
    }
}

int main()
{
    DemonstrateBasicPoolUsage();
    DemonstrateObjectPool();
    DemonstrateFixedSizeAdvantage();
    
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