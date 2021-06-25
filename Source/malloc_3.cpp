#include <unistd.h>
#include <cstring>

#define _METADATA_SIZE sizeof(_MallocMetaData)
#define _LIST_RANGE 1024 // = 1KB
#define _MAX_ALLOC 131071 // = 128KB, the maximum allocatable size on the heap with sbrk().
#define _HIST_SIZE ((_MAX_ALLOC+1)/_LIST_RANGE) // = 128KB/1KB = 128

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

    // Return the next malloc metadata that marks a free payload starting from start.
    // * Bytes will be the size of the payload without the metadata size.
    // If none exist, or start is nullptr - return nullptr.
    _MallocMetaData* getNextFree(_MallocMetaData* start, size_t bytes)
    {
        if(!start)
        {
            return nullptr;
        }
        if(start->is_free && start->size >= bytes)
        {
            return start;
        }
        while(start->next)
        {
            start = start->next;
            if(start->is_free && start->size >= bytes)
            {
                return start;
            }
        }
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
    void histInsert(_MallocMetaData* to_insert) // TODO: histInsert
    {
        // Do magic.
    }

    void histRemove(_MallocMetaData* to_remove) // TODO: histRemove
    {
        // Do some more magic.
    }
    // $$$$$$$$$$ Historgram Functions $$$$$$$$$$ //

    // ********** Mmap List Methods ********** //
    void mmapInsert(_MallocMetaData* to_insert) // TODO: mmapInsert
    {
        // You're a wizard harry!
    }

    void mmapRemove(_MallocMetaData* to_remove) // TODO: mmapRemove
    {
        // Its 'Windgardium Leviosa', not LevioSAAAR!
    }
    // $$$$$$$$$$ Mmap List Methods $$$$$$$$$$ //


    // ********** General Purpose ********** //
    /**
     * Check if the block uses a lot less data than the total payload size and:
     * If it is, split it and return the address of the splitted free metadata struct.
     * Otherwise return NULL.
     */ 
    _MallocMetaData* split(_MallocMetaData* block, size_t in_use) // TODO: split
    {
        // Black magic.
    }

    /**
     * Given a free block as an argument, check if able to merge it to other free blocks
     * surrounding it, and merge if possible.
     * Return true if was able to merge and update new_block to point to the new metadata.
     * Otherwise, return false.
     */ 
    bool mergeFree(_MallocMetaData* block, _MallocMetaData** new_block) // TODO: mergeFree
    {
        // Dragons be here.
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
            _MallocMetaData* tail = getWilderness();
            tail->next = reinterpret_cast<_MallocMetaData*>(prev_brk); // Update the tail's list pointers

            setMetaData(reinterpret_cast<_MallocMetaData*>(prev_brk), size, false, nullptr, tail);
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