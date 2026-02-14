#include <cstdio>
#include <cstring>
#include <vector>
#include "EAllocKit/ArenaAllocator.hpp"
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
    printf("=== Text Editor Undo/Redo System with ArenaAllocator ===\n");
    
    // Create an ArenaAllocator for managing editor state snapshots (4MB arena)
    EAllocKit::ArenaAllocator editorArena(4 * 1024 * 1024);
    
    printf("Text Editor Memory Arena initialized: 4.00 MB\n");
    printf("Demonstrating document editing with checkpoint save/restore...\n\n");
    
    // Document structure
    struct Document {
        char* content;
        size_t contentSize;
        int lineCount;
        int cursorPosition;
    };
    
    // Edit operation metadata
    struct EditInfo {
        const char* operation;
        size_t contentLength;
    };
    
    // Helper function to create document snapshot
    auto createDocument = [&](const char* text, int cursor) -> Document* {
        Document* doc = New<Document>(editorArena);
        if (!doc) return nullptr;
        
        size_t textLen = strlen(text);
        doc->content = static_cast<char*>(editorArena.Allocate(textLen + 1));
        if (!doc->content) return nullptr;
        
        strcpy(doc->content, text);
        doc->contentSize = textLen;
        doc->cursorPosition = cursor;
        
        // Count lines
        doc->lineCount = 1;
        for (size_t i = 0; i < textLen; ++i) {
            if (text[i] == '\n') doc->lineCount++;
        }
        
        return doc;
    };
    
    // Store checkpoints for undo functionality
    std::vector<EAllocKit::ArenaAllocator::Checkpoint> undoStack;
    
    printf("--- Session 1: Initial Document Creation ---\n");
    
    // Save checkpoint before first edit
    auto checkpoint1 = editorArena.SaveCheckpoint();
    undoStack.push_back(checkpoint1);
    
    Document* doc1 = createDocument("Hello World", 11);
    if (doc1) {
        printf("Document created: \"%s\"\n", doc1->content);
        printf("  Lines: %d, Cursor: %d, Size: %zu bytes\n", 
               doc1->lineCount, doc1->cursorPosition, doc1->contentSize);
        printf("  Arena used: %.2f KB\n\n", editorArena.GetUsedBytes() / 1024.0f);
    }
    
    printf("--- Session 2: Adding Content ---\n");
    
    // Save checkpoint before second edit
    auto checkpoint2 = editorArena.SaveCheckpoint();
    undoStack.push_back(checkpoint2);
    
    Document* doc2 = createDocument("Hello World\nThis is a new line.\nEditing text is easy!", 50);
    if (doc2) {
        printf("Document updated:\n\"%s\"\n", doc2->content);
        printf("  Lines: %d, Cursor: %d, Size: %zu bytes\n", 
               doc2->lineCount, doc2->cursorPosition, doc2->contentSize);
        printf("  Arena used: %.2f KB\n\n", editorArena.GetUsedBytes() / 1024.0f);
    }
    
    printf("--- Session 3: Formatting Changes ---\n");
    
    // Save checkpoint before third edit
    auto checkpoint3 = editorArena.SaveCheckpoint();
    undoStack.push_back(checkpoint3);
    
    Document* doc3 = createDocument(
        "HELLO WORLD\n"
        "This is a new line.\n"
        "Editing text is easy!\n"
        "Added more content here.",
        75
    );
    if (doc3) {
        printf("Document after formatting:\n\"%s\"\n", doc3->content);
        printf("  Lines: %d, Cursor: %d, Size: %zu bytes\n", 
               doc3->lineCount, doc3->cursorPosition, doc3->contentSize);
        printf("  Arena used: %.2f KB\n\n", editorArena.GetUsedBytes() / 1024.0f);
    }
    
    // Demonstrate undo functionality using checkpoint restoration
    printf("--- Undo Operation: Restore to Session 2 ---\n");
    
    if (undoStack.size() >= 2) {
        editorArena.RestoreCheckpoint(undoStack[1]);  // Restore to checkpoint2
        printf("Restored to checkpoint 2\n");
        printf("  Arena used after restore: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
        
        // Recreate doc2 state to verify
        Document* restoredDoc = createDocument("Hello World\nThis is a new line.\nEditing text is easy!", 50);
        if (restoredDoc) {
            printf("  Restored document:\n\"%s\"\n", restoredDoc->content);
            printf("  Lines: %d, Size: %zu bytes\n\n", 
                   restoredDoc->lineCount, restoredDoc->contentSize);
        }
    }
    
    // Demonstrate redo by moving to a later checkpoint
    printf("--- Redo Operation: Move to Session 3 ---\n");
    
    if (undoStack.size() >= 3) {
        editorArena.RestoreCheckpoint(undoStack[2]);  // Restore to checkpoint3
        printf("Advanced to checkpoint 3\n");
        printf("  Arena used: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
        
        // Recreate doc3 state
        Document* redoDoc = createDocument(
            "HELLO WORLD\n"
            "This is a new line.\n"
            "Editing text is easy!\n"
            "Added more content here.",
            75
        );
        if (redoDoc) {
            printf("  Document state:\n\"%s\"\n", redoDoc->content);
            printf("  Lines: %d\n\n", redoDoc->lineCount);
        }
    }
    
    // Demonstrate scoped editing with RAII
    printf("--- Session 4: Experimental Edit with Scope Guard ---\n");
    
    {
        // Create RAII scope - automatically restores on scope exit
        auto scope = editorArena.CreateScope();
        
        printf("Entering experimental edit scope (auto-restore on exit)...\n");
        
        Document* experimentalDoc = createDocument(
            "EXPERIMENTAL CHANGES\n"
            "This might not work...\n"
            "Testing some ideas.\n"
            "Will auto-rollback!",
            60
        );
        
        if (experimentalDoc) {
            printf("  Experimental edit:\n\"%s\"\n", experimentalDoc->content);
            printf("  Arena used in scope: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
        }
        
        printf("  Scope ending - auto-restore triggered...\n");
        // Scope guard destructor automatically restores checkpoint
    }
    
    printf("After scope exit - arena automatically restored!\n");
    printf("  Arena used: %.2f KB (back to checkpoint 3)\n\n", editorArena.GetUsedBytes() / 1024.0f);
    
    // Using STL containers with ArenaAllocator
    printf("--- Session 5: Multi-Document Editing with STL ---\n");
    
    auto checkpoint5 = editorArena.SaveCheckpoint();
    
    using StringAllocator = EAllocKit::STLAllocatorAdapter<char, EAllocKit::ArenaAllocator>;
    using DocumentPtrAllocator = EAllocKit::STLAllocatorAdapter<Document*, EAllocKit::ArenaAllocator>;
    
    std::vector<Document*, DocumentPtrAllocator> openDocuments{DocumentPtrAllocator(&editorArena)};
    
    // Open multiple documents
    openDocuments.push_back(createDocument("// main.cpp\n#include <iostream>", 20));
    openDocuments.push_back(createDocument("# README.md\nProject documentation", 25));
    openDocuments.push_back(createDocument("{\n  \"config\": \"value\"\n}", 10));
    
    printf("Opened %zu documents in editor:\n", openDocuments.size());
    for (size_t i = 0; i < openDocuments.size(); ++i) {
        if (openDocuments[i]) {
            printf("  Doc %zu: %zu bytes, %d lines\n", 
                   i + 1, openDocuments[i]->contentSize, openDocuments[i]->lineCount);
        }
    }
    printf("  Total arena used: %.2f KB\n\n", editorArena.GetUsedBytes() / 1024.0f);
    
    // Close all documents by restoring to checkpoint 5
    printf("--- Session 6: Close All Documents ---\n");
    editorArena.RestoreCheckpoint(checkpoint5);
    printf("Restored to before multi-document session\n");
    printf("  Arena used: %.2f KB (all documents freed)\n\n", editorArena.GetUsedBytes() / 1024.0f);
    
    // Demonstrate nested scopes for hierarchical undo
    printf("--- Session 7: Nested Editing Scopes ---\n");
    
    {
        auto outerScope = editorArena.CreateScope();
        printf("Outer scope: Major edit session started\n");
        
        Document* majorEdit = createDocument("Major changes\nOuter scope content", 30);
        printf("  Outer edit: %zu bytes\n", majorEdit ? majorEdit->contentSize : 0);
        
        {
            auto innerScope = editorArena.CreateScope();
            printf("  Inner scope: Trying detailed changes\n");
            
            Document* detailedEdit = createDocument(
                "Major changes\nOuter scope content\nInner scope additions\nMore details here", 
                70
            );
            printf("    Inner edit: %zu bytes\n", detailedEdit ? detailedEdit->contentSize : 0);
            printf("    Arena used in inner scope: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
            
            printf("  Inner scope ending - restoring to outer scope state...\n");
        }
        
        printf("Back to outer scope state\n");
        printf("  Arena used: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
        
        printf("Outer scope ending - restoring to session start...\n");
    }
    
    printf("All nested scopes exited - arena restored!\n");
    printf("  Arena used: %.2f KB\n\n", editorArena.GetUsedBytes() / 1024.0f);
    
    // Final statistics
    printf("--- Final Statistics ---\n");
    printf("Arena capacity: %.2f MB\n", editorArena.GetCapacity() / (1024.0f * 1024.0f));
    printf("Arena used: %.2f KB\n", editorArena.GetUsedBytes() / 1024.0f);
    printf("Arena remaining: %.2f MB\n", editorArena.GetRemainingBytes() / (1024.0f * 1024.0f));
    printf("Is empty: %s\n", editorArena.IsEmpty() ? "Yes" : "No");
    
    printf("\n--- Cleanup: Reset Arena ---\n");
    editorArena.Reset();
    printf("Arena reset - all memory reclaimed instantly!\n");
    printf("Arena is empty: %s\n", editorArena.IsEmpty() ? "Yes" : "No");
    
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
