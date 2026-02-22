#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace aminals
{
    template<size_t capacity>
    class Arena
    {
    public:
        
        template<typename T>
        struct Deleter
        {
            Deleter(Arena* a) : arena(a) {}
            void operator() (T* p) const noexcept
            {
                if (!p) return;
                p->~T();
                arena->release_object();
            }
            
            Arena* arena;
        };

        template <typename T>
        using Ptr = std::unique_ptr<T, Deleter<T>>;


        Arena() :
            buffer({}),
            count(0),
            objects_dispensed(0)
        {
        }
        
        ~Arena()
        {
            this->reset();
        }

        template<class T, class... Args>
        Ptr<T> make_unique(Args&&... args)
        {
            uint8_t* mem = this->push(sizeof(T), alignof(T));
            if (!mem) return Ptr<T>(nullptr, Deleter<T>(this));
            T* obj = ::new (mem) T(std::forward<Args>(args)...);
            this->objects_dispensed++;
            return Ptr<T>(obj, Deleter<T>(this));
        }

        void reset()
        {
            assert(this->objects_dispensed == 0);
            this->count = 0;
        }

        void release_object()
        {
            assert(this->objects_dispensed > 0);
            if (this->objects_dispensed) this->objects_dispensed--;
        }
        
    private:
        
        uint8_t* push(size_t size, size_t align)
        {
            size_t r = this->count % align;
            if (r) this->count += (align - r);

            if (this->count + size > capacity) return nullptr;

            uint8_t* res = &this->buffer[this->count];
            this->count += size;
            return res;
        }        
                      
        std::array<uint8_t, capacity> buffer;
        size_t count;
        size_t objects_dispensed;
    };
}
