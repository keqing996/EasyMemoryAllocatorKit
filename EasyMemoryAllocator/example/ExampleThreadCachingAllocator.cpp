#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "EAllocKit/ThreadCachingAllocator.hpp"

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
    printf("=== Task Scheduler with ThreadCachingAllocator ===\n\n");
    
    // Create a ThreadCachingAllocator for task objects
    EAllocKit::ThreadCachingAllocator taskAllocator;
    
    printf("Task allocator initialized (thread-local caching enabled)\n");
    printf("Running multi-threaded task processing...\n\n");
    
    // Task structure
    struct Task {
        int taskId;
        int threadId;
        int priority;
        char description[64];
        uint64_t timestamp;
    };
    
    std::atomic<int> totalTasksProcessed{0};
    std::atomic<int> totalTasksCreated{0};
    
    printf("--- Phase 1: Single Thread Warm-up ---\n");
    
    // Single thread to initialize caches
    {
        std::vector<Task*> tasks;
        
        for (int i = 0; i < 10; ++i) {
            Task* task = New<Task>(taskAllocator);
            if (task) {
                task->taskId = i;
                task->threadId = 0;
                task->priority = i % 3;
                snprintf(task->description, sizeof(task->description), "Warmup task %d", i);
                task->timestamp = i * 100;
                tasks.push_back(task);
                totalTasksCreated++;
            }
        }
        
        printf("Created %zu tasks in main thread\n", tasks.size());
        
        // Process and free tasks
        for (Task* task : tasks) {
            totalTasksProcessed++;
            Delete(taskAllocator, task);
        }
        
        printf("Processed and freed %d tasks\n\n", totalTasksProcessed.load());
    }
    
    printf("--- Phase 2: Multi-threaded Task Creation ---\n");
    
    const int numThreads = 4;
    const int tasksPerThread = 50;
    
    auto workerFunc = [&](int threadId) {
        std::vector<Task*> localTasks;
        
        // Each thread allocates its own tasks
        for (int i = 0; i < tasksPerThread; ++i) {
            Task* task = New<Task>(taskAllocator);
            if (task) {
                task->taskId = threadId * 1000 + i;
                task->threadId = threadId;
                task->priority = i % 5;
                snprintf(task->description, sizeof(task->description), 
                        "Thread %d task %d", threadId, i);
                task->timestamp = i * 10;
                localTasks.push_back(task);
                totalTasksCreated++;
            }
        }
        
        printf("Thread %d: Created %zu tasks (using thread-local cache)\n", 
               threadId, localTasks.size());
        
        // Simulate some processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Free tasks
        for (Task* task : localTasks) {
            totalTasksProcessed++;
            Delete(taskAllocator, task);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(workerFunc, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    printf("\nTotal tasks created: %d\n", totalTasksCreated.load());
    printf("Total tasks processed: %d\n\n", totalTasksProcessed.load());
    
    printf("--- Phase 3: Producer-Consumer Pattern ---\n");
    
    std::atomic<bool> stopProduction{false};
    std::vector<Task*> sharedQueue;
    std::mutex queueMutex;
    
    // Producer threads
    auto producer = [&](int producerId) {
        for (int i = 0; i < 30; ++i) {
            Task* task = New<Task>(taskAllocator);
            if (task) {
                task->taskId = producerId * 10000 + i;
                task->threadId = producerId;
                task->priority = i % 3;
                snprintf(task->description, sizeof(task->description), 
                        "Producer %d item %d", producerId, i);
                task->timestamp = i * 5;
                
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    sharedQueue.push_back(task);
                }
                
                totalTasksCreated++;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    // Consumer threads
    auto consumer = [&](int consumerId) {
        while (!stopProduction.load() || !sharedQueue.empty()) {
            Task* task = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (!sharedQueue.empty()) {
                    task = sharedQueue.back();
                    sharedQueue.pop_back();
                }
            }
            
            if (task) {
                // Process task
                totalTasksProcessed++;
                
                // Free using thread-local cache
                Delete(taskAllocator, task);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    };
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Start 2 producers and 2 consumers
    for (int i = 0; i < 2; ++i) {
        producers.emplace_back(producer, i);
        consumers.emplace_back(consumer, i);
    }
    
    // Wait for producers
    for (auto& t : producers) {
        t.join();
    }
    
    stopProduction = true;
    
    // Wait for consumers
    for (auto& t : consumers) {
        t.join();
    }
    
    printf("Producer-Consumer completed\n");
    printf("Tasks in queue: %zu (should be 0)\n", sharedQueue.size());
    printf("Total tasks: created=%d, processed=%d\n\n", 
           totalTasksCreated.load(), totalTasksProcessed.load());
    
    printf("--- Phase 4: Burst Allocations ---\n");
    
    auto burstWorker = [&](int workerId) {
        std::vector<Task*> burstTasks;
        
        // Burst allocation
        for (int burst = 0; burst < 3; ++burst) {
            for (int i = 0; i < 100; ++i) {
                Task* task = New<Task>(taskAllocator);
                if (task) {
                    task->taskId = workerId * 100000 + burst * 100 + i;
                    task->threadId = workerId;
                    task->priority = burst;
                    snprintf(task->description, sizeof(task->description), 
                            "Burst %d task %d", burst, i);
                    burstTasks.push_back(task);
                    totalTasksCreated++;
                }
            }
            
            // Process half
            for (size_t i = 0; i < burstTasks.size() / 2; ++i) {
                totalTasksProcessed++;
                Delete(taskAllocator, burstTasks[i]);
            }
            
            // Clear first half
            burstTasks.erase(burstTasks.begin(), burstTasks.begin() + burstTasks.size() / 2);
        }
        
        // Clean up remaining
        for (Task* task : burstTasks) {
            totalTasksProcessed++;
            Delete(taskAllocator, task);
        }
        
        printf("Thread %d: Completed burst allocations\n", workerId);
    };
    
    std::vector<std::thread> burstThreads;
    for (int i = 0; i < 4; ++i) {
        burstThreads.emplace_back(burstWorker, i);
    }
    
    for (auto& t : burstThreads) {
        t.join();
    }
    
    printf("Burst phase completed\n\n");
    
    printf("--- Phase 5: Mixed Size Allocations ---\n");
    
    struct SmallTask {
        int id;
        int data;
    };
    
    struct MediumTask {
        int id;
        char buffer[512];
    };
    
    auto mixedWorker = [&](int workerId) {
        std::vector<void*> allocations;
        
        // Allocate mixed sizes
        for (int i = 0; i < 20; ++i) {
            if (i % 2 == 0) {
                SmallTask* task = New<SmallTask>(taskAllocator);
                if (task) {
                    task->id = i;
                    task->data = workerId;
                    allocations.push_back(task);
                }
            } else {
                MediumTask* task = New<MediumTask>(taskAllocator);
                if (task) {
                    task->id = i;
                    snprintf(task->buffer, sizeof(task->buffer), 
                            "Worker %d medium task %d", workerId, i);
                    allocations.push_back(task);
                }
            }
        }
        
        // Free all
        for (void* ptr : allocations) {
            taskAllocator.Deallocate(ptr);
        }
        
        printf("Thread %d: Mixed allocation completed\n", workerId);
    };
    
    std::vector<std::thread> mixedThreads;
    for (int i = 0; i < 3; ++i) {
        mixedThreads.emplace_back(mixedWorker, i);
    }
    
    for (auto& t : mixedThreads) {
        t.join();
    }
    
    printf("Mixed size phase completed\n\n");
    
    printf("--- Phase 6: Rapid Alloc/Dealloc Cycles ---\n");
    
    auto rapidWorker = [&](int workerId) {
        for (int cycle = 0; cycle < 500; ++cycle) {
            Task* task = New<Task>(taskAllocator);
            if (task) {
                task->taskId = cycle;
                task->threadId = workerId;
                // Immediately free
                Delete(taskAllocator, task);
            }
        }
    };
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> rapidThreads;
    for (int i = 0; i < 4; ++i) {
        rapidThreads.emplace_back(rapidWorker, i);
    }
    
    for (auto& t : rapidThreads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    printf("Rapid cycles completed in %lld ms\n", duration.count());
    printf("4 threads × 500 cycles = 2000 alloc/dealloc pairs\n\n");
    
    printf("--- Final Statistics ---\n");
    printf("Total tasks created: %d\n", totalTasksCreated.load());
    printf("Total tasks processed: %d\n", totalTasksProcessed.load());
    
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
