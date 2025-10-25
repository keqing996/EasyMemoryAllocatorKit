#include <cstdio>
#include "EAllocKit/StackAllocator.hpp"

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
    printf("=== Expression Parser with StackAllocator ===\n\n");
    
    // Create a StackAllocator for parsing temporary data (256KB stack)
    EAllocKit::StackAllocator parseStack(256 * 1024);
    
    printf("Parser stack initialized: 256 KB\n");
    printf("Parsing mathematical expressions with nested function calls...\n\n");
    
    // AST Node structures
    struct NumberNode {
        double value;
    };
    
    struct BinaryOpNode {
        char op;
        void* left;
        void* right;
    };
    
    struct FunctionCallNode {
        char name[32];
        int argCount;
        void** args;
    };
    
    printf("--- Phase 1: Simple Expression ---\n");
    printf("Parsing: 3 + 5\n");
    
    // Allocate left operand
    NumberNode* num1 = New<NumberNode>(parseStack);
    num1->value = 3.0;
    printf("  Allocated number node: %.0f\n", num1->value);
    
    // Allocate right operand
    NumberNode* num2 = New<NumberNode>(parseStack);
    num2->value = 5.0;
    printf("  Allocated number node: %.0f\n", num2->value);
    
    // Allocate operator
    BinaryOpNode* addOp = New<BinaryOpNode>(parseStack);
    addOp->op = '+';
    addOp->left = num1;
    addOp->right = num2;
    printf("  Allocated binary op node: +\n");
    printf("  Result: %.0f\n", num1->value + num2->value);
    
    // Deallocate in stack order (LIFO)
    parseStack.Deallocate(); // addOp
    parseStack.Deallocate(); // num2
    parseStack.Deallocate(); // num1
    printf("  Expression evaluated, stack cleared\n\n");
    
    printf("--- Phase 2: Nested Expression ---\n");
    printf("Parsing: (2 + 3) * (4 - 1)\n");
    
    // Left subexpression: (2 + 3)
    NumberNode* n1 = New<NumberNode>(parseStack);
    n1->value = 2.0;
    
    NumberNode* n2 = New<NumberNode>(parseStack);
    n2->value = 3.0;
    
    BinaryOpNode* addNode = New<BinaryOpNode>(parseStack);
    addNode->op = '+';
    addNode->left = n1;
    addNode->right = n2;
    printf("  Left: (2 + 3) = %.0f\n", n1->value + n2->value);
    
    // Right subexpression: (4 - 1)
    NumberNode* n3 = New<NumberNode>(parseStack);
    n3->value = 4.0;
    
    NumberNode* n4 = New<NumberNode>(parseStack);
    n4->value = 1.0;
    
    BinaryOpNode* subNode = New<BinaryOpNode>(parseStack);
    subNode->op = '-';
    subNode->left = n3;
    subNode->right = n4;
    printf("  Right: (4 - 1) = %.0f\n", n3->value - n4->value);
    
    // Multiply the results
    BinaryOpNode* mulNode = New<BinaryOpNode>(parseStack);
    mulNode->op = '*';
    mulNode->left = addNode;
    mulNode->right = subNode;
    
    double leftResult = n1->value + n2->value;
    double rightResult = n3->value - n4->value;
    printf("  Final: %.0f * %.0f = %.0f\n\n", leftResult, rightResult, leftResult * rightResult);
    
    // Deallocate in reverse order
    for (int i = 0; i < 7; ++i) {
        parseStack.Deallocate();
    }
    
    printf("--- Phase 3: Function Call with Arguments ---\n");
    printf("Parsing: max(10, min(25, 15))\n");
    
    // Inner function: min(25, 15)
    NumberNode* arg1 = New<NumberNode>(parseStack);
    arg1->value = 25.0;
    
    NumberNode* arg2 = New<NumberNode>(parseStack);
    arg2->value = 15.0;
    
    void** minArgs = static_cast<void**>(parseStack.Allocate(sizeof(void*) * 2));
    minArgs[0] = arg1;
    minArgs[1] = arg2;
    
    FunctionCallNode* minFunc = New<FunctionCallNode>(parseStack);
    snprintf(minFunc->name, sizeof(minFunc->name), "min");
    minFunc->argCount = 2;
    minFunc->args = minArgs;
    printf("  Inner: min(%.0f, %.0f) = %.0f\n", arg1->value, arg2->value, 
           arg1->value < arg2->value ? arg1->value : arg2->value);
    
    // Outer function: max(10, result)
    NumberNode* arg3 = New<NumberNode>(parseStack);
    arg3->value = 10.0;
    
    void** maxArgs = static_cast<void**>(parseStack.Allocate(sizeof(void*) * 2));
    maxArgs[0] = arg3;
    maxArgs[1] = minFunc;
    
    FunctionCallNode* maxFunc = New<FunctionCallNode>(parseStack);
    snprintf(maxFunc->name, sizeof(maxFunc->name), "max");
    maxFunc->argCount = 2;
    maxFunc->args = maxArgs;
    
    double innerResult = arg1->value < arg2->value ? arg1->value : arg2->value;
    double outerResult = arg3->value > innerResult ? arg3->value : innerResult;
    printf("  Outer: max(%.0f, %.0f) = %.0f\n\n", arg3->value, innerResult, outerResult);
    
    // Clean up stack
    for (int i = 0; i < 7; ++i) {
        parseStack.Deallocate();
    }
    
    printf("--- Phase 4: Complex Nested Expression ---\n");
    printf("Parsing: ((a + b) * c) - ((d / e) + f)\n");
    
    // Left side: ((a + b) * c)
    NumberNode* a = New<NumberNode>(parseStack);
    a->value = 10.0;
    NumberNode* b = New<NumberNode>(parseStack);
    b->value = 20.0;
    BinaryOpNode* ab = New<BinaryOpNode>(parseStack);
    ab->op = '+';
    
    NumberNode* c = New<NumberNode>(parseStack);
    c->value = 3.0;
    BinaryOpNode* abc = New<BinaryOpNode>(parseStack);
    abc->op = '*';
    
    double leftSide = (a->value + b->value) * c->value;
    printf("  Left: ((%.0f + %.0f) * %.0f) = %.0f\n", a->value, b->value, c->value, leftSide);
    
    // Right side: ((d / e) + f)
    NumberNode* d = New<NumberNode>(parseStack);
    d->value = 100.0;
    NumberNode* e = New<NumberNode>(parseStack);
    e->value = 5.0;
    BinaryOpNode* de = New<BinaryOpNode>(parseStack);
    de->op = '/';
    
    NumberNode* f = New<NumberNode>(parseStack);
    f->value = 10.0;
    BinaryOpNode* def = New<BinaryOpNode>(parseStack);
    def->op = '+';
    
    double rightSide = (d->value / e->value) + f->value;
    printf("  Right: ((%.0f / %.0f) + %.0f) = %.0f\n", d->value, e->value, f->value, rightSide);
    
    // Final subtraction
    BinaryOpNode* finalOp = New<BinaryOpNode>(parseStack);
    finalOp->op = '-';
    
    printf("  Final: %.0f - %.0f = %.0f\n\n", leftSide, rightSide, leftSide - rightSide);
    
    // Clean up in LIFO order
    for (int i = 0; i < 11; ++i) {
        parseStack.Deallocate();
    }
    
    printf("--- Phase 5: Simulating Recursive Descent Parser ---\n");
    printf("Parsing multiple expressions in sequence:\n");
    
    for (int expr = 1; expr <= 5; ++expr) {
        printf("Expression %d: ", expr);
        
        // Parse expression (allocate nodes)
        NumberNode* left = New<NumberNode>(parseStack);
        left->value = expr * 2.0;
        
        NumberNode* right = New<NumberNode>(parseStack);
        right->value = expr * 3.0;
        
        BinaryOpNode* op = New<BinaryOpNode>(parseStack);
        op->op = '+';
        
        printf("%.0f + %.0f = %.0f", left->value, right->value, left->value + right->value);
        
        // Evaluate and clean up
        parseStack.Deallocate();
        parseStack.Deallocate();
        parseStack.Deallocate();
        
        printf(" (stack freed)\n");
    }
    
    printf("\n");
    
    printf("--- Phase 6: Deep Nesting Test ---\n");
    printf("Testing deeply nested expression (10 levels):\n");
    
    // Build nested expression: (((((((((1 + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1)
    NumberNode* base = New<NumberNode>(parseStack);
    base->value = 1.0;
    
    void* current = base;
    for (int level = 1; level <= 10; ++level) {
        NumberNode* next = New<NumberNode>(parseStack);
        next->value = 1.0;
        
        BinaryOpNode* op = New<BinaryOpNode>(parseStack);
        op->op = '+';
        op->left = current;
        op->right = next;
        
        current = op;
        printf("  Level %d allocated\n", level);
    }
    
    printf("  Final result: 1 + 1 + ... (10 times) = 11\n");
    printf("  Unwinding stack...\n");
    
    // Unwind stack (LIFO)
    for (int i = 0; i < 21; ++i) {
        parseStack.Deallocate();
    }
    
    printf("  Stack cleared\n\n");
    
    printf("--- Phase 7: Temporary String Building ---\n");
    
    // Allocate temporary strings on stack
    const char* parts[] = {"Hello", ", ", "World", "!"};
    
    for (int i = 0; i < 4; ++i) {
        size_t len = 0;
        for (const char* p = parts[i]; *p; ++p) len++;
        
        char* temp = static_cast<char*>(parseStack.Allocate(len + 1));
        for (size_t j = 0; j <= len; ++j) {
            temp[j] = parts[i][j];
        }
        
        printf("  Allocated: \"%s\"\n", temp);
    }
    
    printf("  Concatenated result: \"Hello, World!\"\n");
    
    // Clean up strings
    for (int i = 0; i < 4; ++i) {
        parseStack.Deallocate();
    }
    
    printf("  Temporary strings freed\n\n");
    
    printf("--- Phase 8: Array Allocation ---\n");
    
    // Allocate temporary array
    int arraySize = 10;
    double* tempArray = static_cast<double*>(parseStack.Allocate(sizeof(double) * arraySize));
    
    printf("Allocated temporary array[%d]:\n  ", arraySize);
    for (int i = 0; i < arraySize; ++i) {
        tempArray[i] = i * 1.5;
        printf("%.1f ", tempArray[i]);
    }
    printf("\n");
    
    parseStack.Deallocate();
    printf("  Array freed\n\n");
    
    printf("StackAllocator efficiently managed temporary parsing data in LIFO order!\n");
    
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
