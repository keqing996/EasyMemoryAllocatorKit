#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "EAllocKit/FreeListAllocator.hpp"
#include "EAllocKit/STLAllocatorAdapter.hpp"

int main()
{
    printf("=== HTTP Server Connection Pool with FreeListAllocator ===\n");
    
    // Create a FreeListAllocator for managing HTTP connection resources (16MB pool)
    EAllocKit::FreeListAllocator connectionAllocator(16 * 1024 * 1024);
    
    printf("HTTP Server Memory Pool initialized: 16.00 MB\n");
    printf("Simulating incoming HTTP requests with varying payload sizes...\n\n");
    
    // Define connection data structures
    struct HTTPRequest {
        char method[16];          // GET, POST, PUT, DELETE, etc.
        char path[256];           // Request path
        char* headers;            // Variable-size headers
        char* body;               // Variable-size body
        size_t headerSize;
        size_t bodySize;
        int connectionId;
    };
    
    struct HTTPResponse {
        int statusCode;
        char* headers;
        char* body;
        size_t headerSize;
        size_t bodySize;
    };
    
    struct Connection {
        HTTPRequest* request;
        HTTPResponse* response;
        int id;
        bool active;
    };
    
    // STL allocator adapter for managing active connections
    using ConnectionVectorAllocator = EAllocKit::STLAllocatorAdapter<Connection*, EAllocKit::FreeListAllocator>;
    std::vector<Connection*, ConnectionVectorAllocator> activeConnections{ConnectionVectorAllocator(&connectionAllocator)};
    
    auto allocateConnection = [&](int id, const char* method, const char* path, 
                                  size_t headerSize, size_t bodySize) -> Connection* {
        // Allocate connection structure
        Connection* conn = static_cast<Connection*>(connectionAllocator.Allocate(sizeof(Connection)));
        if (!conn) return nullptr;
        
        // Allocate request
        conn->request = static_cast<HTTPRequest*>(connectionAllocator.Allocate(sizeof(HTTPRequest)));
        if (!conn->request) {
            connectionAllocator.Deallocate(conn);
            return nullptr;
        }
        
        // Allocate variable-size headers and body
        conn->request->headers = static_cast<char*>(connectionAllocator.Allocate(headerSize));
        conn->request->body = static_cast<char*>(connectionAllocator.Allocate(bodySize));
        
        // Initialize request data
        strncpy(conn->request->method, method, sizeof(conn->request->method) - 1);
        strncpy(conn->request->path, path, sizeof(conn->request->path) - 1);
        conn->request->headerSize = headerSize;
        conn->request->bodySize = bodySize;
        conn->request->connectionId = id;
        
        // Allocate response
        conn->response = static_cast<HTTPResponse*>(connectionAllocator.Allocate(sizeof(HTTPResponse)));
        conn->id = id;
        conn->active = true;
        
        return conn;
    };
    
    auto deallocateConnection = [&](Connection* conn) {
        if (!conn) return;
        
        if (conn->request) {
            connectionAllocator.Deallocate(conn->request->headers);
            connectionAllocator.Deallocate(conn->request->body);
            connectionAllocator.Deallocate(conn->request);
        }
        
        if (conn->response) {
            connectionAllocator.Deallocate(conn->response->headers);
            connectionAllocator.Deallocate(conn->response->body);
            connectionAllocator.Deallocate(conn->response);
        }
        
        connectionAllocator.Deallocate(conn);
    };
    
    // Simulate multiple request/response cycles
    printf("--- Request Cycle 1: Initial Connections ---\n");
    
    // Small GET request (lightweight)
    Connection* conn1 = allocateConnection(1, "GET", "/api/users", 512, 0);
    if (conn1) {
        activeConnections.push_back(conn1);
        printf("Connection #%d: GET /api/users (headers: 512B, body: 0B)\n", conn1->id);
    }
    
    // POST request with JSON payload (medium)
    Connection* conn2 = allocateConnection(2, "POST", "/api/users/create", 768, 2048);
    if (conn2) {
        activeConnections.push_back(conn2);
        printf("Connection #%d: POST /api/users/create (headers: 768B, body: 2KB)\n", conn2->id);
    }
    
    // Large file upload (heavy)
    Connection* conn3 = allocateConnection(3, "POST", "/api/upload", 1024, 512 * 1024);
    if (conn3) {
        activeConnections.push_back(conn3);
        printf("Connection #%d: POST /api/upload (headers: 1KB, body: 512KB)\n", conn3->id);
    }
    
    // Another small GET
    Connection* conn4 = allocateConnection(4, "GET", "/api/products", 512, 0);
    if (conn4) {
        activeConnections.push_back(conn4);
        printf("Connection #%d: GET /api/products (headers: 512B, body: 0B)\n", conn4->id);
    }
    
    printf("Active connections: %zu\n", activeConnections.size());
    printf("FreeListAllocator manages variable-size allocations efficiently\n\n");
    
    // Simulate completing some requests (free list will manage freed memory)
    printf("--- Request Cycle 2: Complete Some Requests ---\n");
    
    // Complete the file upload (frees large chunk)
    printf("Completing connection #%d (freeing 512KB)...\n", conn3->id);
    deallocateConnection(conn3);
    activeConnections[2] = nullptr;
    
    // Complete one small request
    printf("Completing connection #%d...\n", conn1->id);
    deallocateConnection(conn1);
    activeConnections[0] = nullptr;
    
    printf("Memory freed, available for reuse\n\n");
    
    // New requests arrive - FreeListAllocator reuses freed memory efficiently
    printf("--- Request Cycle 3: New Requests Reuse Memory ---\n");
    
    // Medium POST (should reuse part of freed 512KB)
    Connection* conn5 = allocateConnection(5, "POST", "/api/data", 1024, 64 * 1024);
    if (conn5) {
        activeConnections.push_back(conn5);
        printf("Connection #%d: POST /api/data (headers: 1KB, body: 64KB) - reusing freed memory\n", conn5->id);
    }
    
    // Multiple small requests (efficient for small allocations)
    Connection* conn6 = allocateConnection(6, "GET", "/health", 256, 0);
    Connection* conn7 = allocateConnection(7, "GET", "/metrics", 256, 0);
    Connection* conn8 = allocateConnection(8, "GET", "/status", 256, 0);
    
    if (conn6 && conn7 && conn8) {
        activeConnections.push_back(conn6);
        activeConnections.push_back(conn7);
        activeConnections.push_back(conn8);
        printf("Connections #6-8: GET requests (lightweight, headers: 256B each)\n");
    }
    
    // Large response generation (e.g., API returning dataset)
    Connection* conn9 = allocateConnection(9, "GET", "/api/reports/full", 1024, 0);
    if (conn9) {
        // Allocate large response body
        conn9->response->bodySize = 256 * 1024;
        conn9->response->body = static_cast<char*>(connectionAllocator.Allocate(256 * 1024));
        activeConnections.push_back(conn9);
        printf("Connection #%d: GET /api/reports/full (response body: 256KB)\n", conn9->id);
    }
    
    printf("\nActive connections: %zu\n", activeConnections.size() - 2); // -2 for nullptrs
    printf("Memory fragmentation handled efficiently by FreeListAllocator\n\n");
    
    // Using STL containers with the allocator
    printf("--- Request Cycle 4: Using STL Containers ---\n");
    
    using StringVectorAllocator = EAllocKit::STLAllocatorAdapter<std::string, EAllocKit::FreeListAllocator>;
    
    // Simulate parsing request headers into a vector
    std::vector<std::string, StringVectorAllocator> requestHeaders{StringVectorAllocator(&connectionAllocator)};
    requestHeaders.reserve(10);
    
    requestHeaders.push_back("Host: api.example.com");
    requestHeaders.push_back("User-Agent: Mozilla/5.0");
    requestHeaders.push_back("Accept: application/json");
    requestHeaders.push_back("Content-Type: application/json");
    requestHeaders.push_back("Authorization: Bearer token123");
    
    printf("Parsed %zu request headers using STL vector with FreeListAllocator\n", requestHeaders.size());
    
    // Simulate session data storage
    using IntAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::FreeListAllocator>;
    std::vector<int, IntAllocator> sessionIds{IntAllocator(&connectionAllocator)};
    
    for (int i = 1000; i < 1050; ++i) {
        sessionIds.push_back(i);
    }
    
    printf("Stored %zu active session IDs\n\n", sessionIds.size());
    
    // Cleanup all connections
    printf("--- Cleanup: Closing All Connections ---\n");
    
    for (Connection* conn : activeConnections) {
        if (conn) {
            printf("Closing connection #%d\n", conn->id);
            deallocateConnection(conn);
        }
    }
    
    printf("\nAll connections closed\n");
    
    return 0;
}