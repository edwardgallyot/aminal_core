#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "aminals_disk_streamer.hpp"

using namespace aminals;

static const char* sample_path = "/home/edg/test_sampler/xugufo 2.wav";

// Overlay for the wav header.
struct Wav_Header
{
	struct Data
	{
        char     RIFF[4];
        uint32_t file_size;
        char     WAVE[4];
        char     fmt[4];
        uint32_t format_chunk_size;
        uint16_t format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t bytes_per_frame;
        uint16_t bits_per_sample;
	};

	static Wav_Header from_mapped_file(const juce::MemoryMappedFile& file)
	{
		Data* data = reinterpret_cast<Data*>(file.getData());
		return Wav_Header{ data };
	}
	
	const std::string_view RIFF() const { return std::string_view { d->RIFF, 4 }; }
	bool has_standard_extensions() const { return d->format_chunk_size == Standard_Format_Size; }
	const std::string_view WAVE() const { return std::string_view { d->WAVE, 4 }; }
	const std::string_view fmt() const { return std::string_view { d->fmt, 4 }; }
	uint16_t bytes_per_sample() const { return (d->bits_per_sample >> 3); }
	uint16_t get_safe_num_channels() const { return d->num_channels == 0 ? 1 : d-> num_channels; }
	uint16_t get_safe_bytes_per_frame() const { return d->bytes_per_frame == 0 ? 1 : d-> bytes_per_frame; }
	uint16_t get_safe_bytes_per_sample() const { return get_safe_bytes_per_frame() / get_safe_num_channels(); }
	size_t size() const { return sizeof(Wav_Header::Data); }
	uint32_t get_num_samples() const;
	const Data* get_data() const { return d; }
	void print() const;

private:
	Wav_Header(const Data* data) : d(data)
	{}

	static constexpr size_t Standard_Format_Size = 16; 
	const Data* d;
};

uint32_t Wav_Header::get_num_samples() const
{
	auto num_bytes = d->file_size - sizeof(Wav_Header::Data);
	auto num_samples = num_bytes / this->get_safe_bytes_per_frame();
	return num_samples;
}

void Wav_Header::print() const
{
	std::cout << "RIFF: " << this->RIFF() << std::endl;
	std::cout << "WAVE: " << this->WAVE() << std::endl;
	std::cout << "file_size: " << this->get_data()->file_size << std::endl;
	std::cout << "fmt: " << this->fmt() << std::endl;
	std::cout << "format_chunk_size: " << this->get_data()->format_chunk_size << std::endl;
	std::cout << "format: " << this->get_data()->format << std::endl;
	std::cout << "num_channels: " << this->get_data()->num_channels << std::endl;
	std::cout << "sample_rate: " << this->get_data()->sample_rate << std::endl;
	std::cout << "byte_rate: " << this->get_data()->byte_rate << std::endl;
	std::cout << "bytes_per_frame: " << this->get_data()->bytes_per_frame << std::endl;
	std::cout << "bits_per_sample: " << this->get_data()->bits_per_sample << std::endl;
}

struct Wav_File
{
	static Wav_File from_mapped_file(const juce::MemoryMappedFile& file)
	{
		auto header = Wav_Header::from_mapped_file(file);
		return Wav_File { file, header };
	}
	const Wav_Header& get_header() { return this->header; }
	size_t get_num_can_read(size_t sample_offset);
	void read(float* out, size_t channel, size_t sample_offset, size_t num_samples);
	~Wav_File() = default;
private:
	uint8_t* get_samples_start();
	Wav_File(const juce::MemoryMappedFile& f, const Wav_Header h) : file(f), header(h) {}
	const juce::MemoryMappedFile& file;
	static constexpr auto S24_Max = 0x7FFFFF;
	Wav_Header header;
};


uint8_t* Wav_File::get_samples_start()
{
	return reinterpret_cast<uint8_t*>(this->file.getData()) + this->get_header().size();
}

size_t Wav_File::get_num_can_read(size_t sample_offset)
{
	if (this->header.get_num_samples() > sample_offset)
		return this->header.get_num_samples() - sample_offset;
	return 0;
}

void Wav_File::read(float* out, size_t channel, size_t sample_offset, size_t num_samples)
{
    const uint8_t* start = this->get_samples_start();
    const size_t bytes_per_sample = this->header.get_safe_bytes_per_sample();
    const size_t frame_stride = this->header.get_safe_bytes_per_frame();
    for (size_t sample = 0; sample < num_samples; ++sample)
    {
        const size_t frame_index = sample_offset + sample;
        const size_t read_index = (frame_index * frame_stride) + channel * bytes_per_sample;

        uint32_t raw = 0;
        raw |= uint32_t(start[read_index + 0]);
        raw |= uint32_t(start[read_index + 1]) << 8;
        raw |= uint32_t(start[read_index + 2]) << 16;
		
		// sign extend 24 -> 32
        int32_t in = int32_t(raw << 8) >> 8; 
        out[sample] = float(in) / 8388608.0f;
    }
}

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
			 wait(0)
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
		juce::File file = juce::File(sample_path);
		juce::MemoryMappedFile mapped_file { file, juce::MemoryMappedFile::AccessMode::readWrite };
		
		auto wav_file = Wav_File::from_mapped_file(mapped_file);
		auto header = wav_file.get_header();

		std::cout << "samples per block: " << this->block_size << std::endl;
		header.print(); 

		if (header.get_data()->sample_rate == this->fs)
		{
			std::cout << "sample rate " << sample_rate << " matches" << std::endl;
		}
		else
		{
			std::cout << "sample rate " << sample_rate << " doesn't match" << std::endl;
		}
		
		const auto pre_fetch_blocks = 64;
		size_t sample_count[Disk_Streamer::Impl::Expected_Channels] = { 0, 0 };
		for (size_t block = 0; block < pre_fetch_blocks; ++block)
		{
			bool written[Disk_Streamer::Impl::Expected_Channels] = { false, false };
			for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
			{
				float* samples = this->scratch[channel].data();
				wav_file.read(samples, channel, sample_count[channel], this->block_size);

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
			bool everyone_free = false;
			bool can_write[Disk_Streamer::Impl::Expected_Channels] = { };
			bool can_read[Disk_Streamer::Impl::Expected_Channels] = { };
			while (!everyone_free && this->stream.load())
			{
				everyone_free = true;
				for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
				{
					can_write[channel] = this->queue.get_free_space(channel) >= this->block_size;
					can_read[channel] = wav_file.get_num_can_read(sample_count[channel]) >= this->block_size;
					everyone_free = everyone_free && can_write[channel];

					// for now just loop the test file but we can imagine this being a pre-fetch.
					if (!can_read[channel])
					{
						// reset the sample count 
						sample_count[channel] = 0;
						can_read[channel] = wav_file.get_num_can_read(sample_count[channel]) >= this->block_size;
					}
				}
			
				if (!everyone_free && this->stream.load()) wait.acquire();
			}

			if (!this->stream.load()) break;
			
			bool written[Disk_Streamer::Impl::Expected_Channels] = { };
			for (int channel = 0; channel < Disk_Streamer::Impl::Expected_Channels; ++channel)
			{
				if (can_read[channel])
				{
					wav_file.read(scratch[channel].data(), channel, sample_count[channel], this->block_size);
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
