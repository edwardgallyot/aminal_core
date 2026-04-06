#pragma once
namespace aminals
{
template <typename T> 
struct Span
{
	T* data;
	size_t size;
	T operator[] (size_t i)
	{
		this->data[i];
	}
};
}
