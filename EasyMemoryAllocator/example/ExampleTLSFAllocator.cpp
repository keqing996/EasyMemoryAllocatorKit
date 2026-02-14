#include <cstdio>
#include <vector>
#include "EAllocKit/TLSFAllocator.hpp"

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
    printf("=== Real-time Audio/Video Processing with TLSFAllocator ===\n\n");
    
    // Create TLSFAllocator for real-time media processing (8MB pool)
    // TLSF provides O(1) allocation/deallocation - critical for real-time
    EAllocKit::TLSFAllocator<> mediaAllocator(8 * 1024 * 1024);
    
    printf("Real-time media allocator initialized: 8.00 MB\n");
    printf("TLSF provides O(1) deterministic allocation for real-time constraints\n\n");
    
    // Media buffer structures
    struct AudioBuffer {
        int sampleRate;
        int channels;
        int frameCount;
        float* samples;
        uint64_t timestamp;
    };
    
    struct VideoFrame {
        int width;
        int height;
        int format; // 0=RGB, 1=YUV, 2=RGBA
        uint8_t* pixels;
        uint64_t timestamp;
    };
    
    printf("--- Phase 1: Audio Buffer Processing (48kHz) ---\n");
    
    std::vector<AudioBuffer*> audioBuffers;
    
    // Allocate audio buffers for real-time processing
    // Each buffer = 1024 samples, stereo, 48kHz
    for (int i = 0; i < 10; ++i) {
        AudioBuffer* buf = New<AudioBuffer>(mediaAllocator);
        if (buf) {
            buf->sampleRate = 48000;
            buf->channels = 2;
            buf->frameCount = 1024;
            buf->timestamp = i * 21; // ~21ms per buffer at 48kHz
            
            // Allocate sample data
            size_t sampleDataSize = buf->frameCount * buf->channels * sizeof(float);
            buf->samples = static_cast<float*>(mediaAllocator.Allocate(sampleDataSize));
            
            if (buf->samples) {
                audioBuffers.push_back(buf);
                printf("Audio buffer %d: %d samples, %d ch, %dHz (%.2f KB)\n",
                       i, buf->frameCount, buf->channels, buf->sampleRate,
                       sampleDataSize / 1024.0f);
            }
        }
    }
    
    printf("Allocated %zu audio buffers for real-time streaming\n\n", audioBuffers.size());
    
    printf("--- Phase 2: Video Frame Processing (1080p) ---\n");
    
    std::vector<VideoFrame*> videoFrames;
    
    // Allocate video frames (1920x1080 RGB)
    for (int i = 0; i < 5; ++i) {
        VideoFrame* frame = New<VideoFrame>(mediaAllocator);
        if (frame) {
            frame->width = 1920;
            frame->height = 1080;
            frame->format = 0; // RGB
            frame->timestamp = i * 33; // ~30fps
            
            // Allocate pixel data (RGB = 3 bytes per pixel)
            size_t pixelDataSize = frame->width * frame->height * 3;
            frame->pixels = static_cast<uint8_t*>(mediaAllocator.Allocate(pixelDataSize));
            
            if (frame->pixels) {
                videoFrames.push_back(frame);
                printf("Video frame %d: %dx%d RGB (%.2f MB)\n",
                       i, frame->width, frame->height,
                       pixelDataSize / (1024.0f * 1024.0f));
            }
        }
    }
    
    printf("Allocated %zu video frames\n\n", videoFrames.size());
    
    printf("--- Phase 3: Processing and Releasing (Simulating Pipeline) ---\n");
    
    // Release first 5 audio buffers (processed)
    printf("Processing audio buffers 0-4...\n");
    for (int i = 0; i < 5 && i < static_cast<int>(audioBuffers.size()); ++i) {
        if (audioBuffers[i]) {
            mediaAllocator.Deallocate(audioBuffers[i]->samples);
            Delete(mediaAllocator, audioBuffers[i]);
            audioBuffers[i] = nullptr;
        }
    }
    
    // Release first 2 video frames
    printf("Processing video frames 0-1...\n");
    for (int i = 0; i < 2 && i < static_cast<int>(videoFrames.size()); ++i) {
        if (videoFrames[i]) {
            mediaAllocator.Deallocate(videoFrames[i]->pixels);
            Delete(mediaAllocator, videoFrames[i]);
            videoFrames[i] = nullptr;
        }
    }
    
    printf("Freed memory (TLSF merges adjacent blocks in O(1) time)\n\n");
    
    printf("--- Phase 4: Allocating Mixed Sizes ---\n");
    
    // Small thumbnails (128x72)
    VideoFrame* thumb1 = New<VideoFrame>(mediaAllocator);
    if (thumb1) {
        thumb1->width = 128;
        thumb1->height = 72;
        thumb1->format = 0;
        thumb1->timestamp = 100;
        thumb1->pixels = static_cast<uint8_t*>(
            mediaAllocator.Allocate(128 * 72 * 3));
        printf("Thumbnail: 128x72 (%.2f KB)\n", (128 * 72 * 3) / 1024.0f);
    }
    
    // Medium preview (640x360)
    VideoFrame* preview = New<VideoFrame>(mediaAllocator);
    if (preview) {
        preview->width = 640;
        preview->height = 360;
        preview->format = 0;
        preview->timestamp = 101;
        preview->pixels = static_cast<uint8_t*>(
            mediaAllocator.Allocate(640 * 360 * 3));
        printf("Preview: 640x360 (%.2f KB)\n", (640 * 360 * 3) / 1024.0f);
    }
    
    // Small audio buffer (256 samples)
    AudioBuffer* smallAudio = New<AudioBuffer>(mediaAllocator);
    if (smallAudio) {
        smallAudio->sampleRate = 48000;
        smallAudio->channels = 2;
        smallAudio->frameCount = 256;
        smallAudio->samples = static_cast<float*>(
            mediaAllocator.Allocate(256 * 2 * sizeof(float)));
        printf("Small audio buffer: 256 samples (%.2f KB)\n", 
               (256 * 2 * sizeof(float)) / 1024.0f);
    }
    
    printf("TLSF efficiently handles varying sizes with segregated fit\n\n");
    
    printf("--- Phase 5: Simulating Real-time Frame Pipeline ---\n");
    
    // Simulate continuous frame processing
    for (int cycle = 0; cycle < 3; ++cycle) {
        printf("Pipeline cycle %d:\n", cycle + 1);
        
        // Allocate new frame
        VideoFrame* newFrame = New<VideoFrame>(mediaAllocator);
        if (newFrame) {
            newFrame->width = 1920;
            newFrame->height = 1080;
            newFrame->format = 0;
            newFrame->timestamp = 200 + cycle * 33;
            newFrame->pixels = static_cast<uint8_t*>(
                mediaAllocator.Allocate(1920 * 1080 * 3));
            printf("  Allocated new frame (timestamp: %llu)\n", newFrame->timestamp);
            
            // Process and release immediately (typical real-time pattern)
            mediaAllocator.Deallocate(newFrame->pixels);
            Delete(mediaAllocator, newFrame);
            printf("  Processed and freed frame\n");
        }
        
        // Allocate and process audio
        AudioBuffer* newAudio = New<AudioBuffer>(mediaAllocator);
        if (newAudio) {
            newAudio->sampleRate = 48000;
            newAudio->channels = 2;
            newAudio->frameCount = 1024;
            newAudio->samples = static_cast<float*>(
                mediaAllocator.Allocate(1024 * 2 * sizeof(float)));
            printf("  Allocated audio buffer\n");
            
            mediaAllocator.Deallocate(newAudio->samples);
            Delete(mediaAllocator, newAudio);
            printf("  Processed and freed audio\n");
        }
    }
    
    printf("\n");
    
    printf("--- Phase 6: Different Video Formats ---\n");
    
    // YUV format (requires less memory)
    VideoFrame* yuvFrame = New<VideoFrame>(mediaAllocator);
    if (yuvFrame) {
        yuvFrame->width = 1920;
        yuvFrame->height = 1080;
        yuvFrame->format = 1; // YUV
        // YUV 4:2:0 = 1.5 bytes per pixel
        size_t yuvSize = yuvFrame->width * yuvFrame->height * 3 / 2;
        yuvFrame->pixels = static_cast<uint8_t*>(mediaAllocator.Allocate(yuvSize));
        printf("YUV frame: 1920x1080 (%.2f MB)\n", yuvSize / (1024.0f * 1024.0f));
    }
    
    // RGBA format (requires more memory)
    VideoFrame* rgbaFrame = New<VideoFrame>(mediaAllocator);
    if (rgbaFrame) {
        rgbaFrame->width = 1920;
        rgbaFrame->height = 1080;
        rgbaFrame->format = 2; // RGBA
        size_t rgbaSize = rgbaFrame->width * rgbaFrame->height * 4;
        rgbaFrame->pixels = static_cast<uint8_t*>(mediaAllocator.Allocate(rgbaSize));
        printf("RGBA frame: 1920x1080 (%.2f MB)\n", rgbaSize / (1024.0f * 1024.0f));
    }
    
    printf("Different formats handled efficiently\n\n");
    
    printf("--- Phase 7: Cleanup ---\n");
    
    // Clean up remaining audio buffers
    int audioFreed = 0;
    for (AudioBuffer* buf : audioBuffers) {
        if (buf) {
            mediaAllocator.Deallocate(buf->samples);
            Delete(mediaAllocator, buf);
            audioFreed++;
        }
    }
    printf("Freed %d remaining audio buffers\n", audioFreed);
    
    // Clean up remaining video frames
    int videoFreed = 0;
    for (VideoFrame* frame : videoFrames) {
        if (frame) {
            mediaAllocator.Deallocate(frame->pixels);
            Delete(mediaAllocator, frame);
            videoFreed++;
        }
    }
    printf("Freed %d remaining video frames\n", videoFreed);
    
    // Clean up mixed allocations
    if (thumb1) {
        mediaAllocator.Deallocate(thumb1->pixels);
        Delete(mediaAllocator, thumb1);
    }
    if (preview) {
        mediaAllocator.Deallocate(preview->pixels);
        Delete(mediaAllocator, preview);
    }
    if (smallAudio) {
        mediaAllocator.Deallocate(smallAudio->samples);
        Delete(mediaAllocator, smallAudio);
    }
    if (yuvFrame) {
        mediaAllocator.Deallocate(yuvFrame->pixels);
        Delete(mediaAllocator, yuvFrame);
    }
    if (rgbaFrame) {
        mediaAllocator.Deallocate(rgbaFrame->pixels);
        Delete(mediaAllocator, rgbaFrame);
    }
    
    printf("All media resources freed\n\n");
    
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
