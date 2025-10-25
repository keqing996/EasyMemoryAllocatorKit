#include <cstdio>
#include <cstring>
#include <vector>
#include "EAllocKit/BuddyAllocator.hpp"
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
    printf("=== Game Resource Loading System with BuddyAllocator ===\n");
    
    // Create a BuddyAllocator for managing game resources (16MB pool)
    // BuddyAllocator works best with power-of-2 sizes
    EAllocKit::BuddyAllocator resourcePool(16 * 1024 * 1024);
    
    printf("Resource Pool initialized: %.2f MB\n", 
           resourcePool.GetTotalSize() / (1024.0f * 1024.0f));
    printf("Loading game assets with varying sizes...\n\n");
    
    // Resource structures
    struct Texture {
        char name[64];
        int width;
        int height;
        int channels;
        uint8_t* pixelData;
        size_t dataSize;
    };
    
    struct AudioClip {
        char name[64];
        int sampleRate;
        int channels;
        float duration;
        uint8_t* audioData;
        size_t dataSize;
    };
    
    struct Mesh {
        char name[64];
        int vertexCount;
        int triangleCount;
        float* vertexData;
        uint32_t* indexData;
        size_t vertexDataSize;
        size_t indexDataSize;
    };
    
    // Helper functions to load resources
    auto loadTexture = [&](const char* name, int width, int height) -> Texture* {
        Texture* tex = New<Texture>(resourcePool);
        if (!tex) return nullptr;
        
        snprintf(tex->name, sizeof(tex->name), "%s", name);
        tex->width = width;
        tex->height = height;
        tex->channels = 4; // RGBA
        tex->dataSize = width * height * 4;
        
        // Allocate pixel data
        tex->pixelData = static_cast<uint8_t*>(resourcePool.Allocate(tex->dataSize));
        if (!tex->pixelData) {
            Delete(resourcePool, tex);
            return nullptr;
        }
        
        return tex;
    };
    
    auto loadAudio = [&](const char* name, float duration, int sampleRate) -> AudioClip* {
        AudioClip* audio = New<AudioClip>(resourcePool);
        if (!audio) return nullptr;
        
        snprintf(audio->name, sizeof(audio->name), "%s", name);
        audio->sampleRate = sampleRate;
        audio->channels = 2; // Stereo
        audio->duration = duration;
        audio->dataSize = static_cast<size_t>(duration * sampleRate * audio->channels * sizeof(float));
        
        // Allocate audio data
        audio->audioData = static_cast<uint8_t*>(resourcePool.Allocate(audio->dataSize));
        if (!audio->audioData) {
            Delete(resourcePool, audio);
            return nullptr;
        }
        
        return audio;
    };
    
    auto loadMesh = [&](const char* name, int vertexCount, int triangleCount) -> Mesh* {
        Mesh* mesh = New<Mesh>(resourcePool);
        if (!mesh) return nullptr;
        
        snprintf(mesh->name, sizeof(mesh->name), "%s", name);
        mesh->vertexCount = vertexCount;
        mesh->triangleCount = triangleCount;
        
        // Each vertex: position(3) + normal(3) + uv(2) = 8 floats
        mesh->vertexDataSize = vertexCount * 8 * sizeof(float);
        mesh->indexDataSize = triangleCount * 3 * sizeof(uint32_t);
        
        // Allocate vertex and index data
        mesh->vertexData = static_cast<float*>(resourcePool.Allocate(mesh->vertexDataSize));
        mesh->indexData = static_cast<uint32_t*>(resourcePool.Allocate(mesh->indexDataSize));
        
        if (!mesh->vertexData || !mesh->indexData) {
            resourcePool.Deallocate(mesh->vertexData);
            resourcePool.Deallocate(mesh->indexData);
            Delete(resourcePool, mesh);
            return nullptr;
        }
        
        return mesh;
    };
    
    // Track loaded resources
    std::vector<Texture*> textures;
    std::vector<AudioClip*> audioClips;
    std::vector<Mesh*> meshes;
    
    printf("--- Phase 1: Loading Initial Assets ---\n");
    
    // Load small textures (icons, UI elements)
    textures.push_back(loadTexture("icon_health.png", 64, 64));
    textures.push_back(loadTexture("icon_ammo.png", 64, 64));
    textures.push_back(loadTexture("icon_shield.png", 64, 64));
    
    printf("Loaded 3 small textures (64x64): ~48 KB total\n");
    
    // Load medium textures (character textures)
    textures.push_back(loadTexture("character_diffuse.png", 512, 512));
    textures.push_back(loadTexture("character_normal.png", 512, 512));
    
    printf("Loaded 2 medium textures (512x512): ~2 MB total\n");
    
    // Load audio clips of various lengths
    audioClips.push_back(loadAudio("footstep.wav", 0.3f, 44100));
    audioClips.push_back(loadAudio("gunshot.wav", 0.5f, 44100));
    audioClips.push_back(loadAudio("background_music.ogg", 120.0f, 44100));
    
    printf("Loaded 3 audio clips: ~42 MB total\n");
    
    // Load meshes of different complexity
    meshes.push_back(loadMesh("cube.obj", 24, 12));
    meshes.push_back(loadMesh("character.fbx", 5000, 8000));
    
    printf("Loaded 2 meshes: low-poly cube + character model\n");
    printf("Total resources loaded: %zu textures, %zu audio, %zu meshes\n\n",
           textures.size(), audioClips.size(), meshes.size());
    
    printf("--- Phase 2: Unloading Unused Assets ---\n");
    printf("Scenario: Level transition - unload background music and character\n\n");
    
    // Unload background music (large audio)
    if (audioClips.size() >= 3 && audioClips[2]) {
        printf("Unloading: %s (%.2f MB)\n", 
               audioClips[2]->name, 
               audioClips[2]->dataSize / (1024.0f * 1024.0f));
        resourcePool.Deallocate(audioClips[2]->audioData);
        Delete(resourcePool, audioClips[2]);
        audioClips[2] = nullptr;
    }
    
    // Unload character textures
    if (textures.size() >= 5 && textures[3] && textures[4]) {
        printf("Unloading: %s and %s (~2 MB total)\n",
               textures[3]->name, textures[4]->name);
        resourcePool.Deallocate(textures[3]->pixelData);
        Delete(resourcePool, textures[3]);
        resourcePool.Deallocate(textures[4]->pixelData);
        Delete(resourcePool, textures[4]);
        textures[3] = nullptr;
        textures[4] = nullptr;
    }
    
    printf("Assets unloaded - BuddyAllocator merges freed blocks automatically!\n\n");
    
    printf("--- Phase 3: Loading New Level Assets ---\n");
    printf("Scenario: New level loads - reusing freed memory blocks\n\n");
    
    // Load new level textures (should reuse freed memory)
    textures.push_back(loadTexture("environment_terrain.png", 1024, 1024));
    textures.push_back(loadTexture("environment_sky.png", 512, 512));
    
    printf("Loaded environment textures (reusing freed blocks)\n");
    
    // Load new audio
    audioClips.push_back(loadAudio("ambient_wind.wav", 15.0f, 44100));
    
    printf("Loaded ambient audio\n");
    
    // Load level geometry
    meshes.push_back(loadMesh("level_terrain.obj", 10000, 18000));
    
    printf("Loaded level terrain mesh\n\n");
    
    printf("--- Phase 4: Dynamic Resource Streaming ---\n");
    printf("Scenario: Streaming system loads/unloads based on player position\n\n");
    
    // Simulate loading nearby chunks
    std::vector<Texture*> streamedTextures;
    
    for (int i = 0; i < 5; ++i) {
        char chunkName[64];
        snprintf(chunkName, sizeof(chunkName), "chunk_%d_diffuse.png", i);
        Texture* chunk = loadTexture(chunkName, 256, 256);
        if (chunk) {
            streamedTextures.push_back(chunk);
        }
    }
    
    printf("Streamed in 5 nearby chunks (256x256 each): ~1.25 MB\n");
    
    // Player moves away - unload distant chunks
    printf("Player moved - unloading 3 distant chunks\n");
    for (int i = 0; i < 3 && i < static_cast<int>(streamedTextures.size()); ++i) {
        if (streamedTextures[i]) {
            resourcePool.Deallocate(streamedTextures[i]->pixelData);
            Delete(resourcePool, streamedTextures[i]);
            streamedTextures[i] = nullptr;
        }
    }
    
    printf("Freed memory blocks automatically coalesced by BuddyAllocator\n\n");
    
    printf("--- Phase 5: Using STL Containers ---\n");
    
    using IntAllocator = EAllocKit::STLAllocatorAdapter<int, EAllocKit::BuddyAllocator>;
    std::vector<int, IntAllocator> resourceIds{IntAllocator(&resourcePool)};
    
    // Store resource IDs
    for (int i = 1000; i < 1050; ++i) {
        resourceIds.push_back(i);
    }
    
    printf("Stored %zu resource IDs in STL vector using BuddyAllocator\n", resourceIds.size());
    printf("BuddyAllocator efficiently handles small allocations too!\n\n");
    
    printf("--- Phase 6: Batch Resource Unloading ---\n");
    printf("Scenario: Level complete - unload all level-specific resources\n\n");
    
    // Unload all remaining textures
    int unloadedTextures = 0;
    for (Texture* tex : textures) {
        if (tex) {
            resourcePool.Deallocate(tex->pixelData);
            Delete(resourcePool, tex);
            unloadedTextures++;
        }
    }
    textures.clear();
    
    // Unload all audio
    int unloadedAudio = 0;
    for (AudioClip* audio : audioClips) {
        if (audio) {
            resourcePool.Deallocate(audio->audioData);
            Delete(resourcePool, audio);
            unloadedAudio++;
        }
    }
    audioClips.clear();
    
    // Unload all meshes
    int unloadedMeshes = 0;
    for (Mesh* mesh : meshes) {
        if (mesh) {
            resourcePool.Deallocate(mesh->vertexData);
            resourcePool.Deallocate(mesh->indexData);
            Delete(resourcePool, mesh);
            unloadedMeshes++;
        }
    }
    meshes.clear();
    
    // Unload streamed textures
    for (Texture* tex : streamedTextures) {
        if (tex) {
            resourcePool.Deallocate(tex->pixelData);
            Delete(resourcePool, tex);
        }
    }
    streamedTextures.clear();
    
    printf("Unloaded %d textures, %d audio clips, %d meshes\n",
           unloadedTextures, unloadedAudio, unloadedMeshes);
    printf("All resources freed - memory blocks merged back through buddy system\n\n");
    
    printf("--- Final Statistics ---\n");
    printf("Total pool size: %.2f MB\n", 
           resourcePool.GetTotalSize() / (1024.0f * 1024.0f));
    
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
