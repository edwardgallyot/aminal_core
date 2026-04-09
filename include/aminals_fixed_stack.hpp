#pragma once 

#include <array>
#include <cstddef>

namespace aminals
{
template<typename T, std::size_t N>
struct Fixed_Stack
{
    std::array<T, N> data = {};
    std::size_t top = 0;

    bool push(const T& val) {
        if (top >= N) return false;
        data[top++] = val;
        return true;
    }

    bool pop(T& out) {
        if (top == 0) return false;
        out = data[--top];
        return true;
    }

    bool empty() const { return top == 0; }
    std::size_t size() const { return top; }
};
}
