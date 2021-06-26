#include <unistd.h>
#include <cstring>
#include <cassert>
#define _METADATA_SIZE sizeof(_MallocMetaData)
#define _LIST_RANGE 1024 // = 1KB
#define _MAX_ALLOC 131071 // = 128KB, the maximum allocatable size on the heap with sbrk().
#define _HIST_SIZE ((_MAX_ALLOC+1)/_LIST_RANGE) // = 128KB/1KB = 128
#define _MIN_SPLIT _HIST_SIZE

#define MAX_ALLOC_SIZE 100000000
#define SIZE_TO_INDEX(size) (size/_LIST_RANGE)
#define IS_MMAPPED(metadata) (metadata->size > _MAX_ALLOC)

// The metadata struct of each allocated block
struct _MallocMetaData
{
    size_t size;
    bool is_free;
    _MallocMetaData* next;
    _MallocMetaData* prev;
    _MallocMetaData* next_hist;
    _MallocMetaData* prev_hist;
};

// The struct for the histogram of lists:
struct _ListInfo
{
    int size = 0;
    _MallocMetaData* head = nullptr;
    _MallocMetaData* tail = nullptr;
};

// The singleton class of the allocator, used to manage all allocations:
class _AllocList
{
private:
    _MallocMetaData* head; // The head of the mem-address-ordered doubly linked list
    _MallocMetaData* mmap_head; // The head of the mmapped structs' unordered doubly linked list
    _ListInfo hist[_HIST_SIZE]; // The sizes histogram of the size-ordered doubly linked list

    _MallocMetaData* wilderness;
    size_t num_free_blocks;
    size_t num_free_bytes;
    size_t num_allocated_blocks;
    size_t num_allocated_bytes;
    size_t num_meta_data_bytes;
    size_t size_meta_data;

    _AllocList() : 
    head(nullptr), wilderness(nullptr), num_free_blocks(0), num_free_bytes(0), num_allocated_blocks(0),
    num_allocated_bytes(0), num_meta_data_bytes(0), size_meta_data(_METADATA_SIZE) { }

    _AllocList(_AllocList& other) = delete; // disable copy ctor
    void operator=(_AllocList const &) = delete; // disable = operator

    // Create a metadata struct.
    _MallocMetaData createMetaData(size_t size, bool is_free, _MallocMetaData* next, _MallocMetaData* prev)
    {
        _MallocMetaData res = 
        {
            .size = size,
            .is_free = is_free,
            .next = next,
            .prev = prev
        };
        return res;
    }

    inline _MallocMetaData* getMetaData(void* p)
    {
        return reinterpret_cast<_MallocMetaData*>(reinterpret_cast<char*>(p) - _METADATA_SIZE);
    }

    // Set the metadata members. 
    // NOTICE: The size variable SHOULD NOT include the metadata size.
    void setMetaData(_MallocMetaData* metadata, size_t size, bool is_free, _MallocMetaData* next, _MallocMetaData* prev)
    {
        if(!metadata)
        {
            return;
        }
        metadata->size = size;
        metadata->is_free = is_free;
        metadata->next = next;
        metadata->prev = prev;
    }

    // Calculate the payload address by the metadata address.
    inline void* getPayload(_MallocMetaData* metadata)
    {
        return reinterpret_cast<void*>(reinterpret_cast<char*>(metadata) + _METADATA_SIZE);
    }

    // Return a malloc metadata that marks a free payload.
    // - Argument 'bytes' will be the size of the requested new payload without the metadata size.
    // - The search will be prioritized by size - through the histogram of lists.
    // If none exist, bytes is greater than _MAX_ALLOC(=128KB) - return nullptr.
    // (Therefore, only use this function to search for blocks in the heap)
    _MallocMetaData* getFreeBlock(size_t bytes)
    {
        if(bytes > _MAX_ALLOC)
        {
            return nullptr;
        }

         // This will be the beginning index to search at. 
         // No point in searching in any other list before, because it will definitely be of smaller sizes.
        int index = SIZE_TO_INDEX(bytes);
        _MallocMetaData* curr = nullptr;

        for(int i = index; i < _HIST_SIZE; i++)
        {
            if(hist[i].size <= 0)
            {
                continue;
            }
            curr = hist[i].head; // head is not nullptr because size > 0.
            while(curr)
            {
                // Check if this curr is able to contain the required bytes. 
                // (It is definitely free because hist contains only free blocks)
                if(curr->is_free && bytes <= curr->size)
                {
                    return curr;
                }

                curr = curr->next_hist;
            }
        }

        // We have searched all the relevant lists of free blocks, and found no matching block.
        return nullptr;
    }

    // Return the metadata of the highest addressed metadata in the heap.
    // Return nullptr if the list is empty.
    _MallocMetaData* getWilderness()
    {
        if(!head)
        {
            return nullptr;
        }

        return wilderness;
    }

    // ********** Historgram Methods ********** //
    void histInsert(_MallocMetaData* to_insert)
    {
        int size = to_insert->size;
        int index = SIZE_TO_INDEX(size);
        assert(index < _HIST_SIZE);

        hist[index].size++; // Someone will definitely get in this list one way or another.

        if (hist[index].head == nullptr) // The list is empty at the corresponding index for size
        {
            assert(hist[index].tail == nullptr);
            hist[index].head = to_insert;
            hist[index].tail = to_insert;
            return;
        }

        _MallocMetaData* node = hist[index].head;
        while(node)
        {
            if(to_insert->size <= node->size)
            {
                if (node == hist[index].head) // The new node should be inserted before the current head
                {
                    to_insert->next_hist = hist[index].head;
                    hist[index].head->prev_hist = to_insert;
                    hist[index].head = to_insert;
                    to_insert->prev_hist = nullptr;
                    return;
                }
                // it should be inserted somewhere in the middle, before the current node (not head and not tail)
                node->prev_hist->next_hist = to_insert;
                to_insert->next_hist = node;
                to_insert->prev_hist = node->prev_hist;
                node->prev_hist = to_insert;
                return;
            }
            node = node->next_hist;
        }

        // The new node should be last in the list
        hist[index].tail->next_hist = to_insert;
        to_insert->prev_hist = hist[index].tail;
        to_insert->next_hist = nullptr;
        hist[index].tail = to_insert;
        return;
    }

    void histRemove(_MallocMetaData* to_remove)
    {
        if (!to_remove)
        {
            return;
        }
        int size = to_remove->size;
        int index = SIZE_TO_INDEX(size);
        assert(index < _HIST_SIZE);

        hist[index].size--; // Remove will always be successful.

        if(to_remove == hist[index].head && to_remove == hist[index].tail)
        {
            // This is the only member in the list. Remove it.
            hist[index].head = nullptr;
            hist[index].tail = nullptr;
            return;
        }
        else if(to_remove == hist[index].head)
        {
            // This is not the only member in the list, but it is the first. Remove it.
            hist[index].head->next_hist->prev_hist = nullptr;
            hist[index].head = hist[index].head->next_hist;
            return;
        }
        else if (to_remove == hist[index].tail)
        {
            hist[index].tail->prev_hist->next_hist = nullptr;
            hist[index].tail = hist[index].tail->prev_hist;
            return;
        }

        // This is not the only member in the list, but it is nor the first or the last. Remove it.
        to_remove->prev_hist->next_hist = to_remove->next_hist;
        to_remove->next_hist->prev_hist = to_remove->prev_hist;
        return;
    }
    // $$$$$$$$$$ Historgram Functions $$$$$$$$$$ //

    // ********** Mmap List Methods ********** //
    void mmapInsert(_MallocMetaData* to_insert)
    {
        if (!to_insert || !IS_MMAPPED(to_insert))
        {
            return;
        }
        if(mmap_head == nullptr) // This is the first mmapped region, add it.
        {
            mmap_head = to_insert;
            to_insert->next = nullptr;
            to_insert->prev = nullptr;
            return;
        }
        // otherwise, we will insert it in the head of the list.
        to_insert->next = mmap_head;
        to_insert->prev = nullptr;
        mmap_head->prev = to_insert;
        mmap_head = to_insert;
        return;
    }

    void mmapRemove(_MallocMetaData* to_remove)
    {
        if(to_remove == mmap_head && to_remove->next == nullptr)
        {
            // This is both the first and last member of the list.
            mmap_head = nullptr; // Empty the list.
            return;
        }
        else if (to_remove == mmap_head)
        {
            // This is the first but not last member of the list.
            to_remove->next->prev = nullptr;
            mmap_head = to_remove->next;
            return;
        }
        // This is not the first member of the list.
        to_remove->prev->next = to_remove->next;
        if(to_remove->next != nullptr) // It's not the last member of the list.
        {
            to_remove->next->prev = to_remove->prev;
        }
        return;
    }
    // $$$$$$$$$$ Mmap List Methods $$$$$$$$$$ //


    // ********** General Purpose ********** //
    /**
     * Check if the block uses a lot less data than the total payload size and:
     * If it is, split it and return the address of the splitted free metadata struct.
     * Otherwise return NULL.
     */ 
    _MallocMetaData* split(_MallocMetaData* block, size_t in_use)
    {
        // Check if the leftover size is big enough for a new block of at least _MIN_SPLIT bytes + metadata size bytes:
        if (block->size - in_use >= _MIN_SPLIT + _METADATA_SIZE) 
        {
            block->size = in_use;
            void* split_point = reinterpret_cast<void*>(reinterpret_cast<char*>(block) + in_use);
            setMetaData(reinterpret_cast<_MallocMetaData*>(split_point), block->size-in_use, true, block->next, block);
            block->next = reinterpret_cast<_MallocMetaData*>(split_point);

            // Insert the new freed block to the hist:
            histInsert(reinterpret_cast<_MallocMetaData*>(split_point));
            return reinterpret_cast<_MallocMetaData*>(split_point);
        }

        // Split was not necessary:
        return nullptr;
    }

    /**
     * Given a free block as an argument, check if able to merge it to other free blocks
     * surrounding it, and merge if possible.
     * Return true if was able to merge and update new_block to point to the new metadata.
     * Otherwise, return false.
     * If new_block is null, only return true if the merge is possible in any way or false otherwise.
     */ 
    bool mergeFree(_MallocMetaData* block, _MallocMetaData** new_block)
    {
        if(!block || !block->is_free)
        {
            return false;
        }
        if(block->prev)
        {
            if(block->prev->is_free)
            {
                if(block->next)
                {
                    if(block->next->is_free)
                    {
                        if(new_block) _mergeToSurrounding(block, new_block, true);
                        return true;
                    }
                    else
                    {
                        if(new_block) _mergeToPrev(block, new_block, true);
                        return true;
                    }
                }
                if(new_block) _mergeToPrev(block, new_block, true);
                return true;
            }
        }
        else if(block->next)
        {
            if(block->next->is_free)
            {
                if(new_block) _mergeToNext(block, new_block, true);
                return true;
            }
        }

        // If we get here, no merge was successful.
        return false;
    }

    /**
     * Assumption: block, its prev, and its next - all exist.
     * Will not change the data that was in the original input address of block and its surroundings.
     * Merge all of them to one big block and set new_block to the metadata address of the merged block.
     */
    _MallocMetaData* _mergeToSurrounding(_MallocMetaData* block, _MallocMetaData** new_block, bool to_free)
    {
        assert(block); assert(block->next); assert(block->prev); assert(new_block);
        return _mergeToNext(_mergeToPrev(block, new_block, to_free), new_block, to_free);
    }

    /**
     * Assumption: block and its prev exist.
     * Will not change the data that was in the original input address of block and its surroundings.
     * Merge them to one big block and set new_block to the metadata address of the merged block.
     */
    _MallocMetaData* _mergeToPrev(_MallocMetaData* block, _MallocMetaData** new_block, bool to_free)
    {
        assert(block); assert(block->prev); assert(new_block);
        histRemove(block);
        histRemove(block->prev);

        setMetaData(block->prev, block->prev->size + block->size + _METADATA_SIZE, to_free, block->next, block->prev->prev);
        if(block->prev->next) // NOTE: This is the updated next block for the merged block - if it exists:
        {
            block->prev->next->prev = block->prev;
        }

        histInsert(block->prev);
        *new_block = block->prev;
        return block->prev;
    }

    /**
     * Assumption: block and its next exist.
     * Will not change the data that was in the original input address of block and its surroundings.
     * Merge them to one big block and set new_block to the metadata address of the merged block.
     */
    _MallocMetaData* _mergeToNext(_MallocMetaData* block, _MallocMetaData** new_block, bool to_free)
    {
        assert(block); assert(block->next); assert(new_block);
        histRemove(block);
        histRemove(block->next);

        setMetaData(block, block->size + block->next->size + _METADATA_SIZE, to_free, block->next->next, block->prev);
        if(block->next) // NOTE: This is the updated next block - if it exists:
        {
            block->next->prev = block;
        }

        histInsert(block);
        *new_block = block;
        return block;
    }
    // $$$$$$$$$$ General Purpose $$$$$$$$$$ //

public:
    static _AllocList& getInstance()    // make _AllocList singleton
    {
        static _AllocList instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }
    ~_AllocList() = default;

    // ********** Main Funcs ********** //
    void* smalloc(size_t size)
    {
        if(size == 0 || size > MAX_ALLOC_SIZE)
        {
            return NULL;
        }

        if(head == nullptr) // This is the first allocation made so far
        {
            void* prev_brk = sbrk(size + _METADATA_SIZE);
            if(prev_brk == (void*)-1)
            {
                return NULL;
            }

            head = reinterpret_cast<_MallocMetaData*>(prev_brk);
            setMetaData(head, size, false, nullptr, nullptr);
            wilderness = head;
            num_allocated_blocks++;
            num_allocated_bytes += size;
            num_meta_data_bytes += _METADATA_SIZE;
            return getPayload(head);
        }

        _MallocMetaData* free_block = getNextFree(head, size);
        if(free_block == nullptr) // If there is no free block that can contain size bytes
        {
            void* prev_brk = sbrk(size + _METADATA_SIZE); // Allocate space at the top of the heap
            if(prev_brk == (void*)-1)
            {
                return NULL;
            }
            _MallocMetaData* last_wilderness = wilderness;
            last_wilderness->next = reinterpret_cast<_MallocMetaData*>(prev_brk); // Update the tail's list pointers
            wilderness = reinterpret_cast<_MallocMetaData*>(prev_brk);

            setMetaData(wilderness, size, false, nullptr, last_wilderness);
            num_allocated_blocks++;
            num_allocated_bytes += size;
            num_meta_data_bytes += _METADATA_SIZE;
            return getPayload(reinterpret_cast<_MallocMetaData*>(prev_brk));
        }

        // If there exists a free block that can contain size bytes:
        free_block->is_free = false;
        num_free_blocks--;
        num_free_bytes -= free_block->size;
        return getPayload(free_block);
    }
    
    void* scalloc(size_t num, size_t size)
    {
        void* p = smalloc(num * size);
        if(p == nullptr)
        {
            return nullptr;
        }
        memset(p, 0, num * size);
        return p;
    }

    void sfree(void* p)
    {
        _MallocMetaData* ptr = getMetaData(p);
        if(p == nullptr || ptr->is_free == true)
        {
            return;
        }
        ptr->is_free = true;
        num_free_blocks++;
        num_free_bytes += ptr->size;
    }

    void* srealloc(void* oldp, size_t size)
    {
        if(size == 0 || size > MAX_ALLOC_SIZE)
        {
            return NULL;
        }

        if(oldp == NULL)
        {
            return smalloc(size);
        }

        _MallocMetaData* oldmeta = reinterpret_cast<_MallocMetaData*>(getMetaData(oldp)); 
        if(oldmeta->size >= size)
        {
            return oldp;
        }

        // Look for a free block with enough space:
        _MallocMetaData* free_block = getNextFree(head, size);
        if(free_block == nullptr) // Couldn't find a free block with enough space
        {
            void* prev_brk = sbrk(size + _METADATA_SIZE); // Allocate space at the top of the heap
            if(prev_brk == (void*)-1)
            {
                return NULL;
            }
            _MallocMetaData* old_wilderness = wilderness;
            old_wilderness->next = reinterpret_cast<_MallocMetaData*>(prev_brk); // Update the tail's list pointers
            wilderness = reinterpret_cast<_MallocMetaData*>(prev_brk);

            setMetaData(wilderness, size, false, nullptr, old_wilderness);
            num_allocated_blocks++;
            num_allocated_bytes += size;
            num_meta_data_bytes += _METADATA_SIZE;

            // Copy the old payload:
            memcpy(getPayload(reinterpret_cast<_MallocMetaData*>(prev_brk)), oldp, oldmeta->size);
            
            sfree(oldp); // This will free and update the stats.
            return getPayload(reinterpret_cast<_MallocMetaData*>(prev_brk));
        }
        
        // We found a free block with enough space:
        free_block->is_free = false;
        num_free_blocks--;
        num_free_bytes -= free_block->size;
        memcpy(getPayload(free_block), oldp, oldmeta->size - _METADATA_SIZE);

        sfree(oldp);
        return getPayload(free_block);
    }
    // $$$$$$$$$$ Main Funcs $$$$$$$$$$ //

    // ********** Stats Getters ********** //
    size_t getNumFreeBlocks() const
    {
        return num_free_blocks;
    }

    size_t getNumFreeBytes() const
    {
        return num_free_bytes;
    }

    size_t getNumAllocatedBlocks() const
    {
        return num_allocated_blocks;
    }

    size_t getNumAllocatedBytes() const
    {
        return num_allocated_bytes;
    }

    size_t getNumMetaDataBytes() const
    {
        return num_meta_data_bytes;
    }

    size_t getSizeMetaData() const
    {
        return size_meta_data;
    }
    // $$$$$$$$$$ Stat Getters $$$$$$$$$$ //
};

// ********** The User Functions ********** //
void* smalloc(size_t size)
{
    return _AllocList::getInstance().smalloc(size);
}

void* scalloc(size_t num, size_t size)
{
    return _AllocList::getInstance().scalloc(num, size);
}

void* sfree(void* p)
{
    _AllocList::getInstance().sfree(p);
}

void* srealloc(void* oldp, size_t size)
{
    return _AllocList::getInstance().srealloc(oldp, size);
}
// $$$$$$$$$$ The User Functions $$$$$$$$$$ //

// ***** Statistics private functions: ***** //
size_t _num_free_blocks() 
{
    return _AllocList::getInstance().getNumFreeBlocks();
}

size_t _num_free_bytes() 
{
    return _AllocList::getInstance().getNumFreeBytes();
}

size_t _num_allocated_blocks() 
{
    return _AllocList::getInstance().getNumAllocatedBlocks();
}

size_t _num_allocated_bytes() 
{
    return _AllocList::getInstance().getNumAllocatedBytes();
}

size_t _num_meta_data_bytes() 
{
    return _AllocList::getInstance().getNumMetaDataBytes();
}

size_t _size_meta_data() 
{
    return _AllocList::getInstance().getSizeMetaData();
}
// $$$$$ Statistics private functions: $$$$$ //