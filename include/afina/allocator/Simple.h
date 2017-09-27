#ifndef AFINA_ALLOCATOR_SIMPLE_H
#define AFINA_ALLOCATOR_SIMPLE_H

#include <string>
#include <cstring>

namespace Afina {
namespace Allocator {

// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Pointer;

/**
 * Wraps given memory area and provides defagmentation allocator interface on
 * the top of it.
 */
// TODO: Implements interface to allow usage as C++ allocators
class Simple {
private:
    void* base;
    size_t size;
    struct FreeBlock {
        size_t size;
        FreeBlock* next; // next free block
    };
    FreeBlock* freeBlocksHead;
    static const size_t lastBlockSize = 0;
    void* descriptor;
    size_t nDescriptors;
    void updateDescriptor(void* old_ptr, void* new_ptr);
    void findSurroundingBlocks(void* ptr, void* before, void* after);

public:
    Simple(void *base, size_t size);

    /**
     * TODO: semantics
     * @param N size_t
     */
    Pointer alloc(size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     * @param N size_t
     */
    void realloc(Pointer &p, size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     */
    void free(Pointer &p);

    /**
     * TODO: semantics
     */
    void defrag();

    /**
     * TODO: semantics
     */
    std::string dump() const;
};

} // namespace Allocator
} // namespace Afina
#endif // AFINA_ALLOCATOR_SIMPLE_H
