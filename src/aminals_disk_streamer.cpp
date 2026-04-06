#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "aminals_disk_streamer.hpp"
#include "aminals_span.hpp"

using namespace aminals;

template <size_t Num_Queues, size_t Capacity>
struct Streaming_Queue
{
	void reset_queues();
	static constexpr size_t Memory_Bytes = sizeof(juce::AbstractFifo) * Num_Queues;
	using FifoArena = aminals::Arena<Memory_Bytes>;
	using Buffer = std::array<float, Capacity>;
	using Buffers = std::array<std::array<float, Capacity>, Num_Queues>;
	static constexpr size_t Num_Scratch_Blocks = 32;
    void copy_some_data(float* dest, const float* src, int num_items);
    bool try_write(size_t queue, const float* some_data, int num_items);
    bool try_read(size_t queue, float* some_data, int num_items);
	int get_free_space(size_t queue) { return abstract_fifo[queue]->getFreeSpace(); }
	int get_num_ready(size_t queue) { return abstract_fifo[queue]->getNumReady(); }
	FifoArena arena = {};
	std::array<typename FifoArena::template Ptr<juce::AbstractFifo>, Num_Queues> abstract_fifo = {};
	Buffers buffers = {};
};

template<size_t Num_Queues, size_t Capacity>
void Streaming_Queue<Num_Queues, Capacity>::reset_queues()
{
	arena.reset();
	for (int q = 0; q < Num_Queues; ++q)
	{
		abstract_fifo[q] = arena.template make_unique<juce::AbstractFifo>(Capacity);
	}
}

template<size_t Num_Queues, size_t Capacity>
void Streaming_Queue<Num_Queues, Capacity>::copy_some_data(float* dest, const float* src, int num_items)
{
	juce::FloatVectorOperations::copy(dest, src, num_items);
}

template<size_t Num_Queues, size_t Capacity>
bool Streaming_Queue<Num_Queues, Capacity>::try_write(size_t queue, const float* some_data, int num_items)
{
    const auto scope = abstract_fifo[queue]->write (num_items);
	Buffer* buffer = &buffers[queue];
	if (scope.blockSize1 + scope.blockSize2 != num_items) return false;
	
    if (scope.blockSize1 > 0)
    {
        copy_some_data(buffer->data() + scope.startIndex1, some_data, scope.blockSize1);
    }

    if (scope.blockSize2 > 0)
    {
        copy_some_data(buffer->data() + scope.startIndex2, some_data + scope.blockSize1, scope.blockSize2);
    }
        
    return true;
}

template<size_t Num_Queues, size_t Capacity>
bool Streaming_Queue<Num_Queues, Capacity>::try_read(size_t queue, float* some_data, int num_items)
{
    const auto scope = abstract_fifo[queue]->read (num_items);
	Buffer* buffer = &buffers[queue];
	if (scope.blockSize1 + scope.blockSize2 != num_items) return false;
    if (scope.blockSize1 > 0)
    {
        copy_some_data(some_data, buffer->data() + scope.startIndex1, scope.blockSize1);
    }

    if (scope.blockSize2 > 0)
    {
        copy_some_data(some_data + scope.blockSize1, buffer->data() + scope.startIndex2, scope.blockSize2);
    }
        
    return true;
}



struct Disk_Streamer::Impl
{
    static constexpr size_t Expected_Block_Size = 2048;
    static constexpr size_t Num_Blocks          = 128;
	static constexpr size_t Expected_Channels   = 2;
	
    static constexpr size_t Queue_Size          = Expected_Block_Size * Num_Blocks * Expected_Channels;
	
    static constexpr size_t Num_Out_Blocks      = 1;
	static constexpr size_t Out_Samples         = (Num_Out_Blocks
												   * Expected_Block_Size
												   * Expected_Channels);
	
    static constexpr size_t Num_Scratch_Blocks  = 1;
	static constexpr size_t Scratch_Samples     = (Num_Scratch_Blocks
												   * Expected_Block_Size
												   * Expected_Channels);
 	
    using Queue = Streaming_Queue<Expected_Channels, Queue_Size>;
	
    Impl() : queue(),
			 scratch(),
			 out(),
			 stream(false),
			 thread(std::nullopt),
			 wait(0),
			 filename(nullptr)
    {}
    ~Impl() { stop_streaming(); }
    
    bool start_streaming(double sample_rate, int samples_per_block);
    bool try_get_chunk(float** samples, size_t channel, size_t num_samples);
    bool stop_streaming();
	Queue  queue;
	std::array<std::array<float, Scratch_Samples>, Expected_Channels> scratch;
	std::array<float, Out_Samples> out;
	std::atomic_bool stream;
    std::optional<std::jthread> thread;
	std::binary_semaphore wait;
	char* filename;
	aminals::Span<unsigned long long> offsets;
	uint32_t block_size;
	double fs;
};

bool Disk_Streamer::Impl::start_streaming(double sample_rate, int samples_per_block)
{
	stop_streaming();
	this->stream.store(true);
	this->fs = sample_rate;
	this->block_size = samples_per_block;
	this->queue.reset_queues();
    auto thread_func = [&] () {
		juce::File file = juce::File(this->filename);
		juce::MemoryMappedFile mapped_file { file, juce::MemoryMappedFile::AccessMode::readOnly };
		
		std::cout << "samples per block: " << this->block_size << std::endl;
		
		const auto pre_fetch_blocks = 64;
		size_t sample_count[Disk_Streamer::Impl::Expected_Channels] = { 0, 0 };
		for (size_t block = 0; block < pre_fetch_blocks; ++block)
		{
			bool written[Disk_Streamer::Impl::Expected_Channels] = { false, false };
			for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
			{
				float* samples = this->scratch[channel].data();

                // wav_file.read(scratch[channel].data(), channel, sample_count[channel], this->block_size);
				written[channel] = this->queue.try_write(channel, scratch[channel].data(), this->block_size);
				if(written[channel]) sample_count[channel] += this->block_size;
				if(!written[channel])
				{
					std::cout << "Prefetch failed at: " << channel << " " << sample_count[channel] << std::endl;
				}
			}
		}
		
        while (this->stream.load())
        {
			bool anyone_free = false;
			bool can_write[Disk_Streamer::Impl::Expected_Channels] = { };
			bool can_read[Disk_Streamer::Impl::Expected_Channels] = { };
			while (!anyone_free && this->stream.load())
			{
				for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
				{
					can_write[channel] = this->queue.get_free_space(channel) >= this->block_size;
					can_read[channel] = true; // wav_file.get_num_can_read(sample_count[channel]) >= this->block_size;
					anyone_free = anyone_free || can_write[channel];

					// for now just loop the test file but we can imagine this being a pre-fetch.
					if (!can_read[channel])
					{
						// reset the sample count 
						sample_count[channel] = 0;
						can_read[channel] = true;// wav_file.get_num_can_read(sample_count[channel]) >= this->block_size;
					}
				}
			
				if (!anyone_free && this->stream.load()) wait.acquire();
			 }

			if (!this->stream.load()) break;
			
			bool written[Disk_Streamer::Impl::Expected_Channels] = { };
			for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
			{
				if (can_read[channel] && can_write[channel])
				{
					// wav_file.read(scratch[channel].data(), channel, sample_count[channel], this->block_size);
					written[channel] = this->queue.try_write(channel, scratch[channel].data(), this->block_size);
					if(written[channel])
					{
						sample_count[channel] += this->block_size;
					}
					else
					{
						std::cout << "Write loop failed at: " << channel << " " << sample_count[channel] << std::endl;
					}
				}
			}
		}

		std::cout << "Streamer thread exiting" << std::endl;
		return;
    };
    thread = std::jthread { thread_func };
    return true;
}

bool Disk_Streamer::Impl::try_get_chunk(float** samples, size_t channel, size_t num_samples)
{
	if (this->queue.get_num_ready(channel) >= num_samples
		&& this->queue.try_read(channel, this->out.data(), num_samples))
	{
		wait.release();
	}
	else
	{
		out = {};
	}

	*samples = this->out.data();
	return true;
}

bool Disk_Streamer::Impl::stop_streaming()
{
    if (this->thread)
	{
		this->stream.store(false);
		wait.release();
		if (this->thread->joinable()) this->thread->join();
		this->thread = std::nullopt;
	}
    return true;
}

Disk_Streamer::Disk_Streamer()
    : arena(),
      impl(arena.make_unique<Disk_Streamer::Impl>())
{}


Disk_Streamer::~Disk_Streamer()
{
}

void Disk_Streamer::set_streaming_file(const char* filename)
{
	this->impl->filename = (char*)filename;
}

bool Disk_Streamer::start_streaming(double sample_rate, int samples_per_block)
{
	if (this->impl)
	{
		return this->impl->start_streaming(sample_rate, samples_per_block);
	}
	return false;
}

bool Disk_Streamer::try_get_chunk(float** samples, size_t channel, size_t num_samples)
{
    return this->impl->try_get_chunk(samples, channel, num_samples);
}

bool Disk_Streamer::stop_streaming()
{
	if (this->impl)
	{
		return this->impl->stop_streaming();
	}
	return false;
}

static_assert(Disk_Streamer::MemoryBytes >= sizeof(Disk_Streamer::Impl));
