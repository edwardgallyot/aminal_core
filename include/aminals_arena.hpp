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
    template<size_t capacity = 0>
    class Arena
    {
    public:
        
        template<typename T>
        struct Deleter
        {
            Deleter() = default;
            Deleter(Arena* a) : arena(a) {}
            void operator() (T* p) const noexcept
            {
                if (!p) return;
                p->~T();
            }

            Arena* arena = nullptr;
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
            std::byte* mem = this->push(sizeof(T), alignof(T));
            if (!mem) return Ptr<T>(nullptr, Deleter<T>(this));
            T* obj = ::new (mem) T(std::forward<Args>(args)...);
            return Ptr<T>(obj, Deleter<T>(this));
        }

        void reset()
        {
            this->count = 0;
        }

		static constexpr size_t const_align(size_t bytes, size_t align)
		{
			size_t r = bytes % align;
			if (r) bytes += (align - r);
			return bytes;
		}

    private:
        
        std::byte* push(size_t size, size_t align)
        {
			this->count = const_align(this->count, align);
            if (this->count + size > capacity) return nullptr;

            std::byte* res = &this->buffer[this->count];
            this->count += size;
            return res;
        }        
                      
        alignas(8) std::array<std::byte, capacity> buffer;
        size_t count;
        size_t objects_dispensed;
    };
}
