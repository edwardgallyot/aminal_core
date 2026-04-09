#pragma once

namespace aminals
{
template <typename T> 
struct Span
{
	Span() = default;
	Span(const T* _data, size_t _size)
	{
		data = _data;
		size = _size;
	}
	const T* data = nullptr;
	size_t size = 0;
	const T& operator[] (size_t i)
	{
		return this->data[i];
	}
};
	
template <typename T>
struct Mutable_Span
{
	Mutable_Span() = default;
	Mutable_Span(T* _data, size_t _size)
	{
		data = _data;
		size = _size;
	}
	T* data = nullptr;
	size_t size = 0;
	T& operator[] (size_t i)
	{
		return this->data[i];
	}
};
}
