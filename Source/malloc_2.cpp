#include <unistd.h>
#include <cstring>

#define _METADATA_SIZE sizeof(_MallocMetaData)
#define MAX_ALLOC_SIZE 100000000

// The metadata struct of each allocated block
struct _MallocMetaData
{
    size_t size;
    bool is_free;
    _MallocMetaData* next;
    _MallocMetaData* prev;
};

class _AllocList
{
private:
    _MallocMetaData* head;
    size_t num_free_blocks;
    size_t num_free_bytes;
    size_t num_allocated_blocks;
    size_t num_allocated_bytes;
    size_t num_meta_data_bytes;
    size_t size_meta_data;

    _AllocList() : 
    head(nullptr), num_free_blocks(0), num_free_bytes(0), num_allocated_blocks(0),
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

    // Return the metadata of the tail of the alloc list.
    // Return nullptr if the list is empty.
    _MallocMetaData* getTail()
    {
        if(!head)
        {
            return nullptr;
        }

        _MallocMetaData* curr = head;
        while(curr->next)
        {
            curr = curr->next;
        }
        return curr;
    }

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
            _MallocMetaData* tail = getTail();
            tail->next = reinterpret_cast<_MallocMetaData*>(prev_brk); // Update the tail's list pointers

            setMetaData(reinterpret_cast<_MallocMetaData*>(prev_brk), size, false, nullptr, tail);
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
            _MallocMetaData* tail = getTail();
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
        memcpy(getPayload(free_block), oldp, oldmeta->size);

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