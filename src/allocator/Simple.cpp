#include <afina/allocator/Simple.h>
#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Simple::Simple(void *base, size_t size) : 
    base(base),
    size(size),
    nDescriptors(0)
{
    // create table of descriptors
    nDescriptors ++;
    // first descriptor
    descriptor = (void*)((char*)base + size - lastBlockSize - sizeof(void*));
    *((void**)descriptor) = base;
    // whole memory is one free block
    freeBlocksHead = (FreeBlock*)base;
    freeBlocksHead->size = size - lastBlockSize - nDescriptors * sizeof(void*);
    freeBlocksHead->next = nullptr;
}
//Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {}

/**
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) {
    FreeBlock* prevBlock = nullptr;
    FreeBlock* curBlock = freeBlocksHead;
    // searching among free blocks for the one with proper size
    bool a = false;
    while( curBlock != nullptr )
    {
        if( curBlock->size >= N ) {
            break;
        } else {
            prevBlock = curBlock;
            curBlock = curBlock->next;
        }
    }
    if( curBlock == nullptr ) {
        // perform defrag and check again
        // warning: seems to ruin some defrag tests by defragmentating data while allocating
        /* defrag();
        prevBlock = nullptr;
        curBlock = freeBlocksHead;
        while( curBlock != nullptr ) {
            if( curBlock->size >= N ) {
                break;
            } else {
                prevBlock = curBlock;
                curBlock = curBlock->next;
            }
        }
        if( curBlock == nullptr )*/
        throw AllocError(AllocErrorType::NoMemory, "No memory");
    } else if( prevBlock == nullptr ) {
        // first free block is getting allocated
        if( curBlock->next != nullptr ) {
            freeBlocksHead = curBlock->next; 
        } else {
            freeBlocksHead = (FreeBlock*)((char*)curBlock + N);
            freeBlocksHead->size = curBlock->size - N;
            freeBlocksHead->next = nullptr;  // here curBlock->next == nullptr
        }
    } else {
        if( curBlock->size == N ) {
            prevBlock->next = curBlock->next;
        } else {
            FreeBlock* newBlock = (FreeBlock*)((char*)curBlock + N);
            newBlock->size = curBlock->size - N;
            newBlock->next = curBlock->next;
            prevBlock->next = newBlock;
        }
    }

    // add new descriptor
    // find free cell among descriptors
    void* new_desc = descriptor;  // start from last descriptor
    bool enlargeTable = false;
    for(size_t i = 0; i < nDescriptors; ++i)
    {
        if( *(void**)new_desc == nullptr )
            break;
        else
            new_desc = (void*)((char*)new_desc - sizeof(void*));
        if( i == nDescriptors - 1 )
            enlargeTable = true;
    }
    nDescriptors++;
    *((void**)new_desc) = (void*)((char**)curBlock);
    if( enlargeTable ) {
        // decrease last block size by size of one descriptor
        FreeBlock* lastBlock = freeBlocksHead;
        while( lastBlock->next != nullptr )
            lastBlock = lastBlock->next;
        lastBlock->size -= sizeof(void*);
    }
    // return Pointer containing pointer to descriptor inside
    return Pointer((void**)new_desc);
}

/**
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N)
{
    void* block_start = p.get();
    if( block_start == nullptr )
    {
        // realloc from epty is just an alloc
        p = alloc(N);
        return;
    }
    void* block_end = nullptr;
    FreeBlock* prevprevBlock = nullptr;
    FreeBlock* prevBlock = nullptr;
    FreeBlock* curBlock = freeBlocksHead;
    // find free block before the block pointed by p
    while( curBlock != nullptr )
    {
		if( (char*)curBlock >= (char*)block_start )
            break;
        prevprevBlock = prevBlock;
		prevBlock = curBlock;
		curBlock = curBlock->next;
    }
    // now we know that p is between prevBlock and curBlock
    // check if there are occupied blocks between them
    void* desc = descriptor;
    void* occupiedBefore = (void*)prevBlock; //nullptr;
    void* occupiedAfter = (void*)curBlock; //nullptr;
    void* pdesc = nullptr;
    for(int i = 0; i < nDescriptors; ++i)
    {
        if( occupiedBefore < (char*)(*(void**)desc)
            && (char*)(*(void**)desc) < (char*)block_start ) {
            occupiedBefore = *(void**)desc;
        } else if( (char*)block_start < (char*)(*(void**)desc) 
            && (char*)(*(void**)desc) < occupiedAfter ) {
            occupiedAfter = *(void**)desc;
        }
        if( *(void**)desc == p.get() )
            pdesc = desc;  // BTW find descriptor with which we are going to work
        desc = (void*)((char*)desc - sizeof(void*));
    }
    block_end = occupiedAfter ? occupiedAfter : (void*)((char*)base + size - nDescriptors * sizeof(void*));
    size_t occupiedSize = (char*)block_end - (char*)block_start;
    if( N < occupiedSize )  // shrink block
    {
        if( occupiedAfter == (void*)curBlock ) {
            // if the next block is free, expand it to the left
            size_t blockSize = curBlock->size;
            FreeBlock* nextBlock = curBlock->next;
            curBlock = (FreeBlock*)((char*)curBlock - occupiedSize + N);
            curBlock->next = nextBlock;
            curBlock->size = blockSize + occupiedSize - N;
            if( prevBlock)
                prevBlock->next = curBlock;
            else
                freeBlocksHead = curBlock;
        } else {
            // if the next block is occupied, add new free block
            FreeBlock* newBlock = (FreeBlock*)((char*)block_start + N);
            newBlock->size = occupiedSize - N;
            if( prevBlock ) {
                newBlock->next = prevBlock->next;
                prevBlock->next = newBlock;
            } else {
                newBlock->next = freeBlocksHead;
                freeBlocksHead = newBlock;
            }
        }
    } else {
        // try to expand memory
        size_t availN = 0;
        bool availAfter = occupiedAfter == (void*)curBlock;
        bool availBefore = occupiedBefore == (void*)prevBlock;
        if( occupiedBefore && occupiedBefore == (void*)prevBlock )
            availN += prevBlock->size * availBefore;
        if( occupiedAfter && occupiedAfter == (void*)curBlock )
            availN += curBlock->size * availAfter;
        if( availN > N ) {
            bool flag = true;  // is it needed to move memory backward
            size_t dSize = 0;
            if( availAfter ) {
                // resize forward
                flag = curBlock->size < N - occupiedSize;
                dSize = flag ? curBlock->size : N - occupiedSize;
                FreeBlock* nextBlock = curBlock->next;
                size_t blockSize = curBlock->size;
                if( !flag ) {
                    curBlock = (FreeBlock*)((char*)curBlock + dSize);
                    curBlock->size = blockSize - N + dSize;
                    curBlock->next = nextBlock;
                    if( prevBlock )
                        prevBlock->next = curBlock;
                    else
                        freeBlocksHead = curBlock;
                } else {
                    // delete curBlock completely
                    if( prevBlock )
                        prevBlock->next = nextBlock;
                    else
                        freeBlocksHead = nextBlock;
                }
            }
            if( availBefore && flag ) {
                // move allocated memory backward
                size_t dSize_1 = N - occupiedSize - dSize;
                prevBlock->size -= dSize_1;
                void* old_start = block_start;
                block_start = memmove((char*)prevBlock + prevBlock->size - dSize_1, block_start, occupiedSize + dSize);
                // update descriptor
                updateDescriptor(old_start, block_start);
            }
        } else {
            Pointer p_dest = alloc(N);
            memcpy(p_dest.get(), p.get(), occupiedSize);
            free(p);
            p = p_dest;
        }
    }
    return;
}

/**
 * @param p Pointer
 */
void Simple::free(Pointer &p)
{
    void* block_start = p.get();
    if( block_start == nullptr )
        return;

    void* block_end = nullptr;
    // loop over all blocks and merge with existing free blocks
    FreeBlock* prevBlock = nullptr;
    FreeBlock* curBlock = freeBlocksHead;
    // find free block before the block pointed by p
    while( curBlock != nullptr )
    {
		if( (char*)curBlock >= (char*)block_start )
    		break;
		prevBlock = curBlock;
		curBlock = curBlock->next;
    }
    // now we know that p is between prevBlock and curBlock
    // check if there are occupied blocks between them
    void* desc = descriptor;
    void* occupiedBefore = nullptr;
    void* occupiedAfter = nullptr;
    void* pdesc = nullptr;
    for(int i = 0; i < nDescriptors; ++i)
    {
        if( (occupiedBefore != nullptr ? occupiedBefore : (char*)prevBlock) < (char*)(*(void**)desc)
            && (char*)(*(void**)desc) < (char*)block_start ) {
            occupiedBefore = *(void**)desc;
        } else if( (char*)block_start < (char*)(*(void**)desc) 
            && (char*)(*(void**)desc) < (occupiedAfter != nullptr ? occupiedAfter : (char*)curBlock) ) {
            occupiedAfter = *(void**)desc;
            block_end = occupiedAfter;
        }
        if( *(void**)desc == p.get() )
            pdesc = desc;  // BTW find descriptor with which we are going to work
        desc = (void*)((char*)desc - sizeof(void*));
    }


    // now we know which blocks surrond the block pointed by p
    // we should either merge it with free blocks or mark as new free block
    bool mergeBefore = (prevBlock != nullptr) && (occupiedBefore == nullptr);
    bool mergeAfter = (curBlock != nullptr) && (occupiedAfter == nullptr);
    if( mergeBefore && mergeAfter ) {
        // this block is among two free ones
        prevBlock->next = curBlock->next;
        prevBlock->size = prevBlock->size + ((char*)curBlock - (char*)block_start) + curBlock->size;
    } else if( mergeBefore ) {
        // enlarge prevBlock until occupiedAfter
        prevBlock->size = prevBlock->size + ((char*)curBlock - (char*)block_start);
    } else if( mergeAfter ) {
       size_t newSize = curBlock->size + ((char*)curBlock - (char*)block_start);
       FreeBlock* curBlockNext = curBlock->next;
       curBlock = (FreeBlock*)block_start;
       curBlock->size = newSize;
       curBlock->next = curBlockNext;
    } else {
        // mark this block as free and paste into freelist
        FreeBlock* newBlock = (FreeBlock*)block_start;
        newBlock->size = (block_end == nullptr) ?
            (char*)base + size - nDescriptors*sizeof(void*) - (char*)block_start
            : (char*)block_end - (char*)block_start;
        newBlock->next = curBlock;
        if( prevBlock != nullptr ) {
            prevBlock->next = newBlock;
        } else {
            freeBlocksHead = newBlock;
        }
    }
    // delete corresponding descriptor
    *(void**)pdesc = nullptr;
    // if it was the last descriptor, shrink table
    if( pdesc == (void*)((char*)descriptor - (nDescriptors - 1) * sizeof(void*)) )
        nDescriptors--;
    // otherwise just mark place as free, length of the table doesn't change
}

/**
 */
void Simple::defrag()
{
    // loop over all free blocks and swap them with consequent occupied
    FreeBlock* curBlock = freeBlocksHead;
    while( curBlock->next != nullptr )
    {
        void* occupiedAfter = (void*)((char*)curBlock + curBlock->size);
        if( occupiedAfter == (void*)(curBlock->next) ){
            // all data moved, need to merge free blocks
            curBlock->size += curBlock->next->size;
            curBlock->next = curBlock->next->next;
            continue;
        } else {
            // find corresponding descriptor and occupied block size
            void* desc = descriptor;
            for(int i = 0; i < nDescriptors; ++i) {
                if( *(void**)desc == occupiedAfter ) {
                    break;
                } else {
                    desc = (void*)((char*)desc - sizeof(void*));
                }
            }
            void* next = curBlock->next == nullptr ? (void*)((char*)base + size) : curBlock->next; // either free or occupied block, which goes after this block
            void* temp_desc = descriptor;
            for(int i = 0; i < nDescriptors; ++i) {
                if( *(void**)temp_desc != nullptr
                        && *(void**)temp_desc < (void*)next
                        && *(void**)temp_desc > occupiedAfter )
                    next = *(void**)temp_desc;
                    temp_desc = (void*)((char*)temp_desc - sizeof(void*));
            }
            size_t occupiedSize = (char*)next - (char*)occupiedAfter;
            // store in local memory parameters of free block
            FreeBlock* nextBlock = curBlock->next;
            size_t blockSize = curBlock->size;
            // memmove ocupied data
            occupiedAfter = memcpy((void*)curBlock, occupiedAfter, occupiedSize);
            // store new values in descriptor
            *((void**)desc) = occupiedAfter;
            // re-create FreeBlock at new location
            curBlock = (FreeBlock*)((char*)occupiedAfter + occupiedSize);
            curBlock->size = blockSize;
            curBlock->next = nextBlock;
            freeBlocksHead = curBlock;
        }
    }
}

/**
 * TODO: semantics
 */
std::string Simple::dump() const { return ""; }

void Simple::updateDescriptor(void* old_ptr, void* new_ptr)
{
    // make descriptro which pointed on old_ptr point on new_ptr
    void* desc = descriptor;
    for(int i = 0; i < nDescriptors; ++i) {
        if( *(void**)desc == old_ptr ) {
            break;
        } else {
            desc = (void*)((char*)desc - sizeof(void*));
        }
    }
    *(void**)desc = new_ptr;
}

} // namespace Allocator
} // namespace Afina
