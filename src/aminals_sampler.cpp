#include <iostream>

#include "aminals_sampler.hpp"
#include "aminals_disk_streamer.hpp"
#include "aminals_span.hpp"
#include "aminals_fixed_stack.hpp"

using namespace aminals;

struct Voice
{
	long long sample_id;
	long long voice_id;
};

struct Sampler::Impl
{
    Impl() : voice_requests(),
			 voice_request_sample_ids(),
			 disk_streamer(),
			 midi_map(),
			 sample_names(),
			 voice_assignments(),
			 voices(),
			 free_voices()
    {
		for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
		{
			free_voices.push({ -1, v });
		}
    }
    ~Impl()
    {
    }

	bool try_allocate_voice(Voice& v, long long sample_id)
	{
		if (this->free_voices.pop(v))
		{
			v.sample_id = sample_id;
			this->voice_assignments[v.sample_id] = v.voice_id;
			this->voices[v.voice_id] = v;
			return true;
		}
		return false;
	}

	bool deallocate_sample_id(long long sample_id)
	{
		auto voice_id = this->voice_assignments[sample_id];
		if (voice_id == -1)
		{
			return false;
		}

		this->voices[voice_id].sample_id = -1;
		free_voices.push({-1, voice_id});
		return true;
	}

	void activate_voice(Voice v)
	{
		this->voice_requests[v.voice_id].store(true);
		this->voice_request_sample_ids[v.voice_id].store(v.sample_id);
	}
	
	void deactivate_voice(Voice v)
	{
		this->voice_requests[v.voice_id].store(false);
		this->voice_request_sample_ids[v.voice_id].store(-1);
	}
	
    
	std::array<std::atomic_bool, Disk_Streamer::Num_Voices> voice_requests;
	std::array<std::atomic<long long>, Disk_Streamer::Num_Voices> voice_request_sample_ids;
    Disk_Streamer disk_streamer;
	
	aminals::Span<char> midi_map;
	aminals::Span<const char*> sample_names;
	aminals::Mutable_Span<long long> voice_assignments;
	std::array<Voice, Disk_Streamer::Num_Voices> voices;
	aminals::Fixed_Stack<Voice, Disk_Streamer::Num_Voices> free_voices;
};

Sampler::Sampler()
    : arena(),
      impl(arena.make_unique<Sampler::Impl>())
{
}

Sampler::~Sampler()
{
}

void Sampler::set_streaming_file(const char* filename)
{
	this->impl->disk_streamer.set_streaming_file(filename);
}

void Sampler::set_total_sizes(const unsigned long long* sizes, size_t count)
{
	this->impl->disk_streamer.set_total_sizes(sizes, count);
}

void Sampler::set_voice_assignments(long long* voice_assignments, size_t count)
{
	for (int i = 0; i < count; ++i)
	{
		voice_assignments[i] = -1;
	}
	this->impl->voice_assignments = { voice_assignments, count };
}

void Sampler::set_offsets(const unsigned long long* offsets, size_t count)
{
	this->impl->disk_streamer.set_offsets(offsets, count);
}

void Sampler::set_midi_map(const char* midi_map, size_t count)
{
	this->impl->midi_map = { midi_map, count };
}

void Sampler::set_sample_names(const char** sample_names, size_t count)
{
	this->impl->sample_names = { sample_names, count };
}

void Sampler::set_sample_counts(unsigned long long* sample_counts, size_t count)
{
	this->impl->disk_streamer.set_sample_counts(sample_counts, count);
};

void Sampler::set_channel_strides(const unsigned long long* channel_strides, size_t count)
{
	this->impl->disk_streamer.set_channel_strides(channel_strides, count);
}

void Sampler::prepare(double sample_rate, int samples_per_block)
{
    this->impl->disk_streamer.start_streaming(sample_rate, samples_per_block);
}

void Sampler::process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& midi)
{
    for (const auto& metadata : midi)
    {
        auto message = metadata.getMessage();
		
		auto sample_id = this->impl->midi_map[message.getNoteNumber()];
		auto sample_id_valid = sample_id != -1;
		
		if (!sample_id_valid)
		{
			continue;
		}
		
		if (message.isNoteOn())
		{
			Voice v;
			if (this->impl->try_allocate_voice(v, sample_id))
			{
				std::cout << "Voice: "
						  << v.voice_id
						  << " -> "
						  << this->impl->sample_names[sample_id]
						  << std::endl;
				this->impl->activate_voice(v);
			}
			else
			{
				std::cout << "Out of voices" << std::endl;
			}
		}

		if (message.isNoteOff())
		{
			auto voice_id = this->impl->voice_assignments[sample_id];
			if (this->impl->deallocate_sample_id(sample_id))
			{
				std::cout << "Voice: "
						  << voice_id
						  << " -x "
						  << this->impl->sample_names[sample_id]
						  << std::endl;
				this->impl->deactivate_voice({ voice_id, sample_id });
			}
			else
			{
				std::cout << "Unable to deallocate voice" << std::endl;
			}
		}
    }

	float* streamed_chunk;

	for (int channel = 0; channel < samples.getNumChannels(); ++channel)
	{
		if (!this->impl->disk_streamer.try_get_chunk(&streamed_chunk, channel, samples.getNumSamples()))
		{
			std::cout << "channel " << channel << " failed!" << std::endl;
		}

		// TODO(edg): Vectorise.
		float* write = samples.getWritePointer(channel);
		for (int sample = 0; sample < samples.getNumSamples(); ++sample)
		{
			write[sample] = streamed_chunk[sample];
		}
	}
}

void Sampler::release()
{
    this->impl->disk_streamer.stop_streaming();
}

static_assert(Sampler::MemoryBytes >= sizeof(Sampler::Impl));
