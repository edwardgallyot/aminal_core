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
    const auto scope = abstract_fifo[queue]->write(num_items);
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

struct Stereo_Pcm_Chunk
{
	Stereo_Pcm_Chunk(const float* l, const float* r, size_t num_samples) :
		left(l, num_samples),
		right(r, num_samples)
	{}

	enum class Channel
	{
		Left,
		Right,
	};

	void read(Channel c, size_t sample_count, float const** out);

	const aminals::Span<float> get_channel(Channel c)
	{
		switch (c)
		{
		case Channel::Left: return this->left;
		case Channel::Right: return this->right;
		}
		return {};
	}

	aminals::Span<float> left;
	aminals::Span<float> right;
};

void Stereo_Pcm_Chunk::read(Channel c, size_t sample_count, float const** out)
{
	aminals::Span<float> channel = get_channel(c);
	*out = channel.data + sample_count;
}

struct Disk_Streamer::Impl
{
    static constexpr size_t Expected_Block_Size = 1024;
    static constexpr size_t Num_Blocks          = 4;
	// TODO(edg): We should define "MappedFiles" in an environment file.
	static constexpr size_t Expected_Queues     = Disk_Streamer::Num_Voices*Disk_Streamer::Channels_Per_Voice;

    static constexpr size_t Queue_Size          = Expected_Block_Size * Num_Blocks;
	
    static constexpr size_t Num_Out_Blocks      = 1;
	static constexpr size_t Out_Samples         = (Num_Out_Blocks
												   * Expected_Block_Size
												   * Expected_Queues);
	
    static constexpr size_t Num_Scratch_Blocks  = 1;
	static constexpr size_t Scratch_Samples     = Expected_Block_Size;
												  
 	
    using Queue = Streaming_Queue<Expected_Queues, Queue_Size>;
	
    Impl(std::array<std::atomic_bool, Num_Voices>& _voice_requests,
		 std::array<std::atomic<long long>, Num_Voices>& _voice_request_sample_ids)
		: queue(),
		  scratch(),
		  out(),
		  stream(false),
		  thread(std::nullopt),
		  wait(0),
		  filename(nullptr),
		  offsets(),
		  channel_strides(),
		  sizes(),
		  sample_counts(),
		  block_size(0),
		  voice_requests(_voice_requests),
		  voice_request_sample_ids(_voice_request_sample_ids)
	{}
    ~Impl() { stop_streaming(); }
    
    bool start_streaming(double sample_rate, int samples_per_block);
    bool try_get_chunk(float** samples, long long voice_id, size_t channel, size_t num_samples);
    bool stop_streaming();
	Queue  queue;
	std::array<std::array<float, Scratch_Samples>, Disk_Streamer::Channels_Per_Voice> scratch;
	std::array<float, Out_Samples> out;
	std::atomic_bool stream;
    std::optional<std::jthread> thread;
	std::binary_semaphore wait;
	char* filename;
	aminals::Span<unsigned long long> offsets;
	aminals::Span<unsigned long long> channel_strides;
	aminals::Span<unsigned long long> sizes;
	aminals::Mutable_Span<unsigned long long> sample_counts;
	uint32_t block_size;
	double fs;
	std::array<std::atomic_bool, Num_Voices>& voice_requests;
	std::array<std::atomic<long long>, Num_Voices>& voice_request_sample_ids;
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

		if (mapped_file.getData())
		{
			std::cout << "successfully mapped: " << this->filename << std::endl;
		}
		else
		{
			// TODO(edg): How do we handle this error? We probably want to alert the user.
			std::cout << "couldn't map: " << this->filename << std::endl;
		}
		
		std::cout << "samples per block: " << this->block_size << std::endl;
		
        while (this->stream.load())
        {
			bool anyone_free = false;
			bool can_write[Disk_Streamer::Num_Voices][Disk_Streamer::Channels_Per_Voice] = { };
			while (!anyone_free && this->stream.load())
			{
				for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
				{
					if (voice_requests[v].load() && voice_request_sample_ids[v] != -1)
					{
						for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
						{
							can_write[v][channel] =
								this->queue.get_free_space(channel + (Disk_Streamer::Channels_Per_Voice * v)) >= this->block_size;
							anyone_free = anyone_free || can_write[channel];
						}

					}
				}
				if (!anyone_free && this->stream.load()) wait.acquire();
			}
			
			if (!this->stream.load()) break;
			
			bool written[Disk_Streamer::Num_Voices][Disk_Streamer::Channels_Per_Voice] = { };
			for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
			{
				for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
				{
					auto atomic_sample_id = voice_request_sample_ids[v].load();
					if (can_write[v][channel] && atomic_sample_id  != -1)
					{
						written[v][channel] = this->queue.try_write(
							(Disk_Streamer::Channels_Per_Voice * v) + channel,
							scratch[channel].data(),
							this->block_size);
						if(written[v][channel])
						{
							this->sample_counts[atomic_sample_id] += this->block_size;
							std::cout << "STREAM "
									  << v
									  << " -> SAMPLE ID "
									  << atomic_sample_id
									  << std::endl;
						}
						else
						{
							std::cout << "Write loop failed at: " << channel << " " << this->sample_counts[channel] << std::endl;
						}
					}}
				
			}
		}

		std::cout << "Streamer thread exiting" << std::endl;
		return;
    };
    thread = std::jthread { thread_func };
    return true;
}

bool Disk_Streamer::Impl::try_get_chunk(float** samples, long long voice_id, size_t channel, size_t num_samples)
{
	auto queue = (Disk_Streamer::Channels_Per_Voice * voice_id) + channel;
	auto res = false;
	if (this->queue.get_num_ready(queue) >= num_samples
		&& this->queue.try_read(queue, this->out.data(), num_samples))
	{
		wait.release();
		res = true;
	}
	else
	{
		out = {};
	}
	wait.release();

	*samples = this->out.data();
	return res;
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

Disk_Streamer::Disk_Streamer(
	std::array<std::atomic_bool, Num_Voices>& voice_requests,
	std::array<std::atomic<long long>, Num_Voices>& voice_request_sample_ids)
    : arena(),
      impl(arena.make_unique<Disk_Streamer::Impl>(voice_requests, voice_request_sample_ids))
{}


Disk_Streamer::~Disk_Streamer()
{
}

void Disk_Streamer::set_streaming_file(const char* filename)
{
	this->impl->filename = (char*)filename;
}

void Disk_Streamer::set_total_sizes(const unsigned long long* sizes, size_t count)
{
	this->impl->sizes = { sizes, count };
}

void Disk_Streamer::set_offsets(const unsigned long long* offsets, size_t count)
{
	this->impl->offsets = { offsets, count };
}

void Disk_Streamer::set_sample_counts(unsigned long long* sample_counts, size_t count)
{
	this->impl->sample_counts = { sample_counts, count };
}
	
void Disk_Streamer::set_channel_strides(const unsigned long long* channel_strides, size_t count)
{
	this->impl->channel_strides = { channel_strides, count };
}

bool Disk_Streamer::start_streaming(double sample_rate, int samples_per_block)
{
	if (this->impl)
	{
		return this->impl->start_streaming(sample_rate, samples_per_block);
	}
	return false;
}

bool Disk_Streamer::try_get_chunk(float** samples, long long voice_id, size_t channel, size_t num_samples)
{
    return this->impl->try_get_chunk(samples, voice_id, channel, num_samples);
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
