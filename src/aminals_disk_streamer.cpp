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
	using Buffers = std::array<Buffer, Num_Queues>;
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

	if (scope.blockSize1 + scope.blockSize2 != num_items) return false;
	Buffer* buffer = &buffers[queue];	
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
    const auto scope = abstract_fifo[queue]->read(num_items);
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
	Stereo_Pcm_Chunk(float* l, float* r, size_t num_samples) :
		left(l, num_samples),
		right(r, num_samples)
	{}
	
	Stereo_Pcm_Chunk() = default;

	enum class Channel
	{
		Left,
		Right,
	};

	static Channel channel_from_index(size_t i) { return i == 0 ? Channel::Left : Channel::Right; }
	void read(Channel c, float** out, size_t sample_count);
	const aminals::Mutable_Span<float> get_channel(Channel c);
	size_t get_num_can_read(Channel c, size_t sample_count);
	aminals::Mutable_Span<float> left = {};
	aminals::Mutable_Span<float> right = {};
};

void Stereo_Pcm_Chunk::read(Channel c, float** out, size_t sample_count)
{
	aminals::Mutable_Span<float> channel = get_channel(c);

	*out = (channel.data + sample_count);
}

size_t Stereo_Pcm_Chunk::get_num_can_read(Channel c, size_t sample_count)
{
	aminals::Mutable_Span<float> channel = get_channel(c);
	return sample_count < channel.size
		? (channel.size - sample_count)
		: 0;
}

const aminals::Mutable_Span<float> Stereo_Pcm_Chunk::get_channel(Channel c)
{
	switch (c)
	{
	case Channel::Left: return this->left;
	case Channel::Right: return this->right;
	}
	return {};
}

struct Disk_Streamer::Impl
{
    static constexpr size_t Expected_Block_Size = 1024;
    static constexpr size_t Num_Blocks          = 64;
	static constexpr size_t Expected_Queues     = Disk_Streamer::Num_Voices*Disk_Streamer::Channels_Per_Voice;

    static constexpr size_t Queue_Size          = Expected_Block_Size * Num_Blocks;
	
    static constexpr size_t Num_Out_Blocks      = 1;
	static constexpr size_t Out_Samples         = (Num_Out_Blocks
												   * Expected_Block_Size
												   * Expected_Queues);

    static constexpr size_t Num_Scratch_Blocks  = 1;
	static constexpr size_t Scratch_Samples     = Expected_Block_Size;
	
    using Queue = Streaming_Queue<Expected_Queues, Queue_Size>;
	using Arena = aminals::Arena<aminals::Arena<MemoryBytes>::const_align(sizeof(juce::MemoryMappedFile), 8)>;
	
    Impl(std::array<std::atomic<Disk_Streamer::Voice_State>, Num_Voices>& _voice_requests,
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
		  write_sample_counts(),
		  read_sample_counts(),
		  block_size(0),
		  voice_requests(_voice_requests),
		  voice_request_sample_ids(_voice_request_sample_ids),
		  pre_fetch_buffers(),
		  pre_fetch_size(0),
		  pre_fetch_count(0),
		  mapped_data(nullptr),
		  file(std::nullopt),
		  arena(),
		  mapped_file()
	{}
    ~Impl() { stop_streaming(); }
    
    bool start_streaming(double sample_rate, int samples_per_block);
    bool try_get_chunk(float** samples, long long voice_id, size_t channel, size_t num_samples);
    bool stop_streaming();

	Stereo_Pcm_Chunk get_pcm_chunk_for_sample_id(long long id);
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
	std::array<unsigned long long, Num_Voices> write_sample_counts;
	std::array<unsigned long long, Num_Voices> read_sample_counts;
	uint32_t block_size;
	double fs;
	std::array<std::atomic<Disk_Streamer::Voice_State>, Num_Voices>& voice_requests;
	std::array<std::atomic<long long>, Num_Voices>& voice_request_sample_ids;
	aminals::Mutable_Span<float> pre_fetch_buffers;
	size_t pre_fetch_size;
	size_t pre_fetch_count;
	uint8_t* mapped_data;
	std::optional<juce::File> file;
	Arena arena;
	Arena::Ptr<juce::MemoryMappedFile> mapped_file;
};

Stereo_Pcm_Chunk Disk_Streamer::Impl::get_pcm_chunk_for_sample_id(long long id)
{
	auto left = reinterpret_cast<float*>(
		&this->mapped_data[this->offsets[id]]);
	auto right = reinterpret_cast<float*>(
		&this->mapped_data[this->offsets[id]
					 + this->channel_strides[id]]);
	Stereo_Pcm_Chunk chunk = {
		left,
		right,
		this->channel_strides[id]
	};
	return chunk;
}


bool Disk_Streamer::Impl::start_streaming(double sample_rate, int samples_per_block)
{
	stop_streaming();
	this->stream.store(true);
	this->fs = sample_rate;
	this->block_size = samples_per_block;
	this->queue.reset_queues();
	
	this->file = juce::File(this->filename);
	this->mapped_file = this->arena.make_unique<juce::MemoryMappedFile>(*(this->file), juce::MemoryMappedFile::AccessMode::readOnly);
	this->mapped_data = static_cast<uint8_t*>(mapped_file->getData());
	
	for (int buffer = 0; buffer < this->pre_fetch_count; ++buffer)
	{
		auto chunk = get_pcm_chunk_for_sample_id(buffer);
		for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
		{
			auto offset = ((buffer * Disk_Streamer::Channels_Per_Voice) + channel) * this->pre_fetch_size;

			float* read;
			chunk.read(Stereo_Pcm_Chunk::channel_from_index(channel), &read, 0);
			for (int sample = 0; sample < this->pre_fetch_size; ++sample)
			{
				this->pre_fetch_buffers[offset + sample] = read[sample];
			}
		}
	}
		
	if (this->mapped_data)
	{
		std::cout << "successfully mapped: " << this->filename << std::endl;
	}
	else
	{
		// TODO(edg): How do we handle this error? We probably want to alert the user.
		std::cout << "couldn't map: " << this->filename << std::endl;
	}
		
	std::cout << "samples per block: " << this->block_size << std::endl;
		
    auto thread_func = [&] () {
		
		bool written[Disk_Streamer::Num_Voices][Disk_Streamer::Channels_Per_Voice] = { };
        while (this->stream.load())
        {
			bool anyone_free = false;
			bool can_write[Disk_Streamer::Num_Voices][Disk_Streamer::Channels_Per_Voice] = { };
			bool can_read[Disk_Streamer::Num_Voices][Disk_Streamer::Channels_Per_Voice] = { };
			Stereo_Pcm_Chunk pcm_chunks[Disk_Streamer::Num_Voices] = {};

			while (!anyone_free  && this->stream.load())
			{
				for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
				{
					auto atomic_sample_id = voice_request_sample_ids[v].load();
					auto state = voice_requests[v].load();
					
					if (state == Disk_Streamer::Voice_State::Stop_Request)
					{
						voice_requests[v].store(Disk_Streamer::Voice_State::Stopping);
						this->write_sample_counts[v] = this->pre_fetch_size;
						pcm_chunks[v] = {};
						anyone_free = false;
						for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
						{
							can_read[v][channel] = false;
							can_write[v][channel] = false;
						}
					}
					
					if (state == Disk_Streamer::Voice_State::Start_Request)
					{
						voice_requests[v].store(Disk_Streamer::Voice_State::Started);
					}
					
					if (state == Disk_Streamer::Voice_State::Started)
					{
						pcm_chunks[v] = this->get_pcm_chunk_for_sample_id(atomic_sample_id);

						for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
						{
							can_read[v][channel] =
								pcm_chunks[v].get_num_can_read(Stereo_Pcm_Chunk::channel_from_index(channel),
															   this->write_sample_counts[v]) >= this->block_size;
							can_write[v][channel] =
								this->queue.get_free_space(channel + (Disk_Streamer::Channels_Per_Voice * v)) >= this->block_size;
					
							anyone_free = anyone_free || (can_write[v][channel] && !written[v][channel]);
						}
					}
				}

				
				if (!anyone_free && this->stream.load()) wait.acquire();
			}
			
			if (!this->stream.load()) break;

			for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
			{
				bool increment = false;
				auto atomic_sample_id = voice_request_sample_ids[v].load();
				for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
				{
					if (written[v][channel]) continue;

					if(can_write[v][channel] && can_read[v][channel] && atomic_sample_id != -1)
					{
						float* scratch_samples;
						pcm_chunks[v].read(Stereo_Pcm_Chunk::channel_from_index(channel),
									   &scratch_samples,
									   this->write_sample_counts[v]);
						written[v][channel] = this->queue.try_write(
							(v * Disk_Streamer::Channels_Per_Voice) + channel,
							scratch_samples,
							this->block_size);
#if 0						
						std::cout << "STREAM "
								  << v
								  << " -> SAMPLE ID "
								  << atomic_sample_id
								  << " - COUNT "
								  << this->sample_counts[atomic_sample_id]
								  << std::endl;
#endif
					}
				}

				bool all_written = true;
				for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
				{
					all_written = all_written && written[v][channel];
				}
				
				if (all_written)
				{
					this->write_sample_counts[v] += this->block_size;
					for (int channel = 0; channel < Disk_Streamer::Channels_Per_Voice; ++channel)
					{
						written[v][channel] = false;
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

bool Disk_Streamer::Impl::try_get_chunk(float** samples, long long voice_id, size_t channel, size_t num_samples)
{
	auto res = false;
	auto sample_id = this->voice_request_sample_ids[voice_id].load();
	auto queue = (Disk_Streamer::Channels_Per_Voice * voice_id) + channel;
	auto can_read_from_queue = this->queue.get_num_ready(queue) >= num_samples;
	auto can_read_from_pre_fetch = this->read_sample_counts[voice_id] < (this->pre_fetch_size - this->block_size);
	
	if (can_read_from_pre_fetch)
	{
		auto offset = (((sample_id * Disk_Streamer::Channels_Per_Voice) + channel) + this->read_sample_counts[voice_id]);
		*samples = &this->pre_fetch_buffers[offset];
		res = true;
	}
	else if (can_read_from_queue)
	{
		res = this->queue.try_read(queue, this->out.data(), num_samples);
		*samples = this->out.data();
	}
	else
	{
		out = {};
	}
	if (res) this->read_sample_counts[voice_id] += block_size;
	wait.release();	
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
	this->mapped_file.reset();
	this->arena.reset();
    return true;
}

Disk_Streamer::Disk_Streamer(
	std::array<std::atomic<Disk_Streamer::Voice_State>, Num_Voices>& voice_requests,
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
	
void Disk_Streamer::set_channel_strides(const unsigned long long* channel_strides, size_t count)
{
	this->impl->channel_strides = { channel_strides, count };
}

void Disk_Streamer::set_pre_fetch_buffers(float* buffers, size_t count, size_t buffer_size)
{
	this->impl->pre_fetch_buffers = { buffers, count * buffer_size };
	this->impl->pre_fetch_size = buffer_size;
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

void Disk_Streamer::reset_read_head(long long voice_id)
{
	this->impl->read_sample_counts[voice_id] = 0;
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
