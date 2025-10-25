#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include "EAllocKit/LinearAllocator.hpp"
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
    printf("=== Game Frame-Based Memory Allocation with LinearAllocator ===\n");
    
    // Create a LinearAllocator for frame-based memory management (8MB per frame)
    EAllocKit::LinearAllocator frameAllocator(8 * 1024 * 1024);
    
    printf("Game Engine Frame Memory System\n");
    printf("Frame allocator initialized: %.2f MB available\n", frameAllocator.GetAvailableSpaceSize() / (1024.0f * 1024.0f));
    printf("Simulating game frames with temporary allocations...\n\n");
    
    // STL allocator adapters for different data types
    using FloatVectorAllocator = EAllocKit::STLAllocatorAdapter<float, EAllocKit::LinearAllocator>;
    using IntVectorAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::LinearAllocator>;
    using Vec3VectorAllocator = EAllocKit::STLAllocatorAdapter<float[3], EAllocKit::LinearAllocator>;
    
    // Simulate multiple game frames
    for (int frame = 1; frame <= 5; ++frame) {
        printf("--- Game Frame %d ---\n", frame);
        
        // 1. Allocate vertex buffer for rendering (raw allocation)
        const size_t vertexCount = 1000 + (frame * 200);  // More vertices each frame
        struct Vertex { float x, y, z, nx, ny, nz, u, v; };  // 32 bytes per vertex
        
        Vertex* vertices = static_cast<Vertex*>(frameAllocator.Allocate(vertexCount * sizeof(Vertex)));
        if (vertices) {
            // Generate vertex data
            for (size_t i = 0; i < vertexCount; ++i) {
                float angle = static_cast<float>(i) / vertexCount * 6.28318f; // 2*PI
                vertices[i] = {
                    cosf(angle) * (frame * 0.5f),           // x
                    sinf(angle) * (frame * 0.5f),           // y  
                    static_cast<float>(frame),               // z
                    cosf(angle), sinf(angle), 0.0f,         // normals
                    static_cast<float>(i % 100) / 100.0f,   // u
                    static_cast<float>(static_cast<int>(i)) / 1000.0f     // v
                };
            }
            printf("  Allocated vertex buffer: %zu vertices (%.2f KB)\n", 
                   vertexCount, (vertexCount * sizeof(Vertex)) / 1024.0f);
        }
        
        // 2. Allocate visible object indices using STL vector
        std::vector<int, IntVectorAllocator> visibleObjects{IntVectorAllocator(&frameAllocator)};
        visibleObjects.reserve(500);
        
        // Simulate frustum culling - add visible object IDs
        for (int i = 0; i < 800; ++i) {
            if ((i + frame) % 3 != 0) {  // Simulate 2/3 objects being visible
                visibleObjects.push_back(i);
            }
        }
        printf("  Visible objects: %zu/%d (using STL vector with LinearAllocator)\n", 
               visibleObjects.size(), 800);
        
        // 3. Allocate light data using STL vector
        std::vector<float, FloatVectorAllocator> lightData{FloatVectorAllocator(&frameAllocator)};
        const int lightCount = 50 + (frame * 10);
        lightData.reserve(lightCount * 7);  // 7 floats per light (pos + color + intensity)
        
        for (int i = 0; i < lightCount; ++i) {
            float radius = static_cast<float>(i) * 0.3f;
            float angle = static_cast<float>(i) * 0.5f;
            
            // Position (xyz), Color (rgb), Intensity
            lightData.insert(lightData.end(), {
                cosf(angle) * radius,                    // x
                sinf(angle) * radius,                    // y
                static_cast<float>(frame) * 2.0f,        // z
                0.8f + (i % 3) * 0.1f,                  // r
                0.7f + ((i + 1) % 3) * 0.1f,            // g  
                0.9f + ((i + 2) % 3) * 0.1f,            // b
                1.0f - (static_cast<float>(i) / lightCount) * 0.5f  // intensity
            });
        }
        printf("  Light system: %d lights (%.2f KB, using STL vector)\n", 
               lightCount, (lightData.size() * sizeof(float)) / 1024.0f);
        
        // 4. Allocate particle system data
        struct Particle { 
            float x, y, z;          // position
            float vx, vy, vz;       // velocity
            float life, age;        // lifetime data
            float size;             // particle size
            int type;               // particle type
        };
        
        const size_t particleCount = 2000 + (frame * 500);
        Particle* particles = static_cast<Particle*>(
            frameAllocator.Allocate(particleCount * sizeof(Particle), 16)  // 16-byte aligned for SIMD
        );
        
        if (particles) {
            // Initialize particle system
            for (size_t i = 0; i < particleCount; ++i) {
                float speed = static_cast<float>(rand()) / RAND_MAX * 5.0f;
                float angle = static_cast<float>(rand()) / RAND_MAX * 6.28318f;
                
                particles[i] = {
                    static_cast<float>(rand()) / RAND_MAX * 20.0f - 10.0f,  // x: -10 to 10
                    static_cast<float>(rand()) / RAND_MAX * 20.0f - 10.0f,  // y: -10 to 10
                    static_cast<float>(frame) * 3.0f,                       // z
                    cosf(angle) * speed,                                     // vx
                    sinf(angle) * speed,                                     // vy
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f,  // vz
                    3.0f + static_cast<float>(rand()) / RAND_MAX * 2.0f,     // life
                    0.0f,                                                    // age
                    0.1f + static_cast<float>(rand()) / RAND_MAX * 0.5f,     // size
                    rand() % 4                                               // type
                };
            }
            printf("  Particle system: %zu particles (%.2f KB, 16-byte aligned)\n", 
                   particleCount, (particleCount * sizeof(Particle)) / 1024.0f);
        }
        
        // 5. Allocate UI rendering data using STL vector
        std::vector<float, FloatVectorAllocator> uiVertices{FloatVectorAllocator(&frameAllocator)};
        
        // Simulate UI elements: health bars, minimap, buttons, text
        const int uiElements = 25 + (frame * 3);
        uiVertices.reserve(uiElements * 24);  // 6 vertices * 4 floats per quad
        
        for (int i = 0; i < uiElements; ++i) {
            float x = static_cast<float>(i % 10) * 100.0f;
            float y = static_cast<float>(i) / 10.0f * 60.0f;
            
            // Generate quad vertices (2 triangles)
            std::vector<float> quad = {
                x, y, 0.0f, 1.0f,           // top-left
                x + 80, y, 0.0f, 1.0f,      // top-right  
                x, y + 50, 0.0f, 1.0f,      // bottom-left
                x + 80, y, 0.0f, 1.0f,      // top-right
                x + 80, y + 50, 0.0f, 1.0f, // bottom-right
                x, y + 50, 0.0f, 1.0f       // bottom-left
            };
            uiVertices.insert(uiVertices.end(), quad.begin(), quad.end());
        }
        printf("  UI system: %d elements (%.2f KB, using STL vector)\n", 
               uiElements, (uiVertices.size() * sizeof(float)) / 1024.0f);
        
        // 6. Allocate temporary string buffers for text rendering
        char* debugStrings = static_cast<char*>(frameAllocator.Allocate(1024 * 16)); // 16KB for strings
        if (debugStrings) {
            // Simulate various debug/UI text generation
            snprintf(debugStrings, 1024 * 16, 
                "Frame: %d | Vertices: %zu | Lights: %d | Particles: %zu | UI Elements: %d | "
                "FPS: %.1f | Memory Used: %.2f MB", 
                frame, vertexCount, lightCount, particleCount, uiElements,
                60.0f - frame * 2.0f,  // Simulate decreasing FPS
                (8.0f * 1024.0f * 1024.0f - frameAllocator.GetAvailableSpaceSize()) / (1024.0f * 1024.0f)
            );
            printf("  Debug text: %.2f KB allocated for strings\n", 16.0f);
        }
        
        printf("  Frame memory usage: %.2f MB / %.2f MB (%.1f%% used)\n",
               (8.0f * 1024.0f * 1024.0f - frameAllocator.GetAvailableSpaceSize()) / (1024.0f * 1024.0f),
               8.0f,
               ((8.0f * 1024.0f * 1024.0f - frameAllocator.GetAvailableSpaceSize()) / (8.0f * 1024.0f * 1024.0f)) * 100.0f);
        
        // End of frame: Reset allocator for next frame (instant cleanup!)
        frameAllocator.Reset();
        printf("  Frame complete - all temporary memory instantly reclaimed!\n\n");
    }
    
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