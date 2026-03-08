#include <array>
#include <chrono>
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
		return data;
	}
	
	const std::string_view RIFF() const { return std::string_view { d->RIFF, 4 }; }
	bool has_standard_extensions() const { return d->format_chunk_size == Standard_Format_Size; }
	const std::string_view WAVE() const { return std::string_view { d->WAVE, 4 }; }
	const std::string_view fmt() const { return std::string_view { d->fmt, 4 }; }
	size_t size() const { return sizeof(Wav_Header::Data); }
	const Data* get_data() const { return d; }

	void print() const
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
	
private:
	Wav_Header(const Data* data) : d(data)
	{}

	static constexpr size_t Standard_Format_Size = 16; 
	const Data* d;
};

struct Wav_File
{
	static Wav_File from_mapped_file(const juce::MemoryMappedFile& file)
	{
		auto header = Wav_Header::from_mapped_file(file);
		return Wav_File { file, header };
	}
	const Wav_Header& get_header() { return this->header; }
	float* get_samples_start();
	void read(float** out, size_t channel, size_t num_samples);
	~Wav_File() = default;
private:
	Wav_File(const juce::MemoryMappedFile& f, const Wav_Header h) : file(f), header(h)
    {}
	const juce::MemoryMappedFile& file;
	Wav_Header header;
};


float* Wav_File::get_samples_start()
{
	return reinterpret_cast<float*>(
		reinterpret_cast<uint8_t*>(this->file.getData()) + this->get_header().size());
}

void Wav_File::read(float** out, size_t channel, size_t num_samples)
{
	auto start = this->get_samples_start();

	
}

template <size_t capacity>
struct Streaming_Queue
{
	static constexpr size_t Num_Scratch_Blocks = 32;
    void copy_some_data(float* dest, const float* src, int num_items);
    bool try_write(const float* some_data, int num_items);
    bool try_read(float* some_data, int num_items);
    juce::AbstractFifo abstractFifo { capacity };
    std::array<float, capacity> buffer {};
};

template<size_t capacity>
void Streaming_Queue<capacity>::copy_some_data(float* dest, const float* src, int num_items)
{
	juce::FloatVectorOperations::copy(dest, src, num_items);
}

template<size_t capacity>
bool Streaming_Queue<capacity>::try_write(const float* some_data, int num_items)
{
    const auto scope = abstractFifo.write (num_items);

    if (scope.blockSize1 > 0)
    {
        copy_some_data(buffer.data() + scope.startIndex1, some_data, scope.blockSize1);
        return true; 
    }

    if (scope.blockSize2 > 0)
    {
        copy_some_data(buffer.data() + scope.startIndex2, some_data + scope.blockSize1, scope.blockSize2);
        return true;
    }
        
    return false;
}

template<size_t capacity>
bool Streaming_Queue<capacity>::try_read(float* some_data, int num_items)
{
    const auto scope = abstractFifo.read (num_items);

    if (scope.blockSize1 > 0)
    {
        copy_some_data(some_data, buffer.data() + scope.startIndex1, scope.blockSize1);
        return true;
    }

    if (scope.blockSize2 > 0)
    {
        copy_some_data(some_data + scope.blockSize1, buffer.data() + scope.startIndex2, scope.blockSize2);
        return true;
    }
        
    return false;
}

struct Disk_Streamer::Impl
{
    static constexpr size_t Block_Size         = 512;
    static constexpr size_t Num_Blocks         = 128;
	static constexpr size_t Expected_Channels  = 2;
	
    static constexpr size_t Queue_Size         = Block_Size * Num_Blocks * Expected_Channels;
	
    static constexpr size_t Num_Out_Blocks     = 1;
	static constexpr size_t Out_Samples        = (Num_Out_Blocks
												  * Block_Size
												  * Expected_Channels);
	
    static constexpr size_t Num_Scratch_Blocks = 1;
	static constexpr size_t Scratch_Samples    = (Num_Scratch_Blocks
												  * Block_Size
												  * Expected_Channels);
 	
    using Queue = Streaming_Queue<Queue_Size>;
	
    Impl() : queue(),
			 scratch(),
			 out(),
			 stream(false),
			 thread(std::nullopt)
    {}
    ~Impl() { }
    
    bool start_streaming();
    bool try_get_chunk(float** samples, size_t channel, size_t num_samples);
    bool stop_streaming();
    Queue queue;
 	std::array<float, Scratch_Samples> scratch;
	std::array<float, Out_Samples> out;
	std::atomic_bool stream;
    std::optional<std::jthread> thread;
};

bool Disk_Streamer::Impl::start_streaming()
{
	this->stream.store(true);
    auto thread_func = [&] () {
		juce::File file = juce::File(sample_path);
		juce::MemoryMappedFile mapped_file { file, juce::MemoryMappedFile::AccessMode::readWrite };
		
		auto wav_file = Wav_File::from_mapped_file(mapped_file);
		auto header = wav_file.get_header();
		header.print();
		
		const auto pre_fetch_blocks = 8;
		size_t sample_count = 0;

		for (size_t block = 0; block < pre_fetch_blocks; ++block)
		{
			if (!this->queue.try_write(scratch.data(), scratch.size()))
				std::cout << "fail" << std::endl;
		}

		
        while (this->stream.load())
        {
			this->queue.try_write(scratch.data(), scratch.size());
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		std::cout << "we are exiting" << std::endl;
    };
    thread = std::jthread { thread_func };
    return true;
}

bool Disk_Streamer::Impl::try_get_chunk(float** samples, size_t channel, size_t num_samples)
{
	*samples = out.data();
	if (!this->queue.try_read(out.data(), out.size()))
	{
		std::cout << "nah m8" << std::endl;
	}
	return true;
}

bool Disk_Streamer::Impl::stop_streaming()
{
    if (this->thread)
	{
		this->stream.store(false);
		if (this->thread->joinable()) this->thread->join();
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

bool Disk_Streamer::start_streaming()
{
	if (this->impl)
	{
		return this->impl->start_streaming();
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
