#include <bitset>
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
			 disk_streamer(voice_requests, voice_request_sample_ids),
			 midi_map(),
			 sample_names(),
			 voice_assignments(),
			 voices(),
			 free_voices(),
			 adsrs()
    {
		for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
		{
			this->free_voices.push({ .sample_id = -1, .voice_id = v });
			this->voices[v].sample_id = -1;
			this->adsrs[v].setParameters({ 1.0f, 0.0f, 1.0f, 1.0f });
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

	bool try_deallocate_voice(Voice& v)
	{
		if (this->free_voices.push({-1, v.voice_id}))
		{
			this->voices[v.voice_id].sample_id = -1;
			return true;
		}
		return false;
	}

	bool activate_voice(Voice v)
	{
		if (this->voice_requests[v.voice_id].load() == Disk_Streamer::Voice_State::Stopped)
		{
		    this->voice_request_sample_ids[v.voice_id].store(v.sample_id);
		    this->voice_requests[v.voice_id].store(Disk_Streamer::Voice_State::Start_Request);
			return true;
		}
		return false;
	}
	
	bool deactivate_voice(Voice v)
	{
		if (this->voice_requests[v.voice_id].load() == Disk_Streamer::Voice_State::Started)
		{
			this->voice_requests[v.voice_id].store(Disk_Streamer::Voice_State::Stop_Request);
			return true;
		}
		return false;
	}
    
	std::array<std::atomic<Disk_Streamer::Voice_State>, Disk_Streamer::Num_Voices> voice_requests;
	std::array<std::atomic<long long>, Disk_Streamer::Num_Voices> voice_request_sample_ids;
	std::bitset<Disk_Streamer::Num_Voices> deactivation_pending;
	std::bitset<Disk_Streamer::Num_Voices> activation_pending;
    Disk_Streamer disk_streamer;
	
	aminals::Span<char> midi_map;
	aminals::Span<const char*> sample_names;
	aminals::Mutable_Span<long long> voice_assignments;
	std::array<Voice, Disk_Streamer::Num_Voices> voices;
	aminals::Fixed_Stack<Voice, Disk_Streamer::Num_Voices> free_voices;
	std::array<juce::ADSR, Disk_Streamer::Num_Voices> adsrs;
	std::vector<float> adsr_buffer;
};

Sampler::Sampler()
    : arena(),
      impl(arena.make_unique<Sampler::Impl>())
{
}

Sampler::~Sampler()
{
}

void Sampler::set_attack(float attack_seconds)
{
	for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
	{
		auto p = this->impl->adsrs[v].getParameters();
		if (p.attack != attack_seconds)
		{
			this->impl->adsrs[v].setParameters({
				attack_seconds,
				p.decay,
				p.sustain,
				p.release
			});
		}
	}
}

void Sampler::set_release(float release_seconds)
{
	for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
	{
		auto p = this->impl->adsrs[v].getParameters();
		if (p.release != release_seconds)
		{
			this->impl->adsrs[v].setParameters({
				p.attack,
				p.decay,
				p.sustain,
				release_seconds
				});
		}
		
	}
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

void Sampler::set_channel_strides(const unsigned long long* channel_strides, size_t count)
{
	this->impl->disk_streamer.set_channel_strides(channel_strides, count);
}

void Sampler::set_pre_fetch_buffers(float* buffers, size_t count, size_t buffer_size)
{
	this->impl->disk_streamer.set_pre_fetch_buffers(buffers, count, buffer_size);
}

void Sampler::prepare(double sample_rate, int samples_per_block)
{
    this->impl->disk_streamer.start_streaming(sample_rate, samples_per_block);
	this->impl->adsr_buffer.resize(samples_per_block);
	for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
	{
		this->impl->adsrs[v].setSampleRate(sample_rate);
	}
}

void Sampler::process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& midi)
{

	// Firstly, process the midi data. We can use the
	// input to make our requests to the disk streamer.
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
#if 0
				std::cout << "Voice: "
						  << v.voice_id
						  << " -> "
						  << this->impl->sample_names[sample_id]
						  << std::endl;
#endif
				if (!this->impl->activate_voice(v))
				{
					this->impl->activation_pending[v.voice_id] = false;
					this->impl->try_deallocate_voice(v);
				}
				else
				{
					this->impl->adsrs[v.voice_id].noteOn();
				}
			}
			else
			{
				std::cout << "Out of voices" << std::endl;
			}
		}

		if (message.isNoteOff())
		{
			for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
			{
				auto voice = this->impl->voices[v];

				if (this->impl->midi_map[message.getNoteNumber()] == voice.sample_id)
				{
					this->impl->adsrs[v].noteOff();
				}
			}
		}
    }


	for (int channel = 0; channel < samples.getNumChannels(); ++channel)
	{
		float* write = samples.getWritePointer(channel);
		for (int sample = 0; sample < samples.getNumSamples(); ++sample)
		{
			write[sample] = 0.0f;
		}
	}

	// Now stream in samples from the disk_streamer
	float* streamed_chunk;
	for (int v = 0; v < Disk_Streamer::Num_Voices; ++v)
	{
		auto voice = this->impl->voices[v];
		auto state = this->impl->voice_requests[v].load();
		
		if (this->impl->activation_pending[v])
		{
			this->impl->activation_pending[v] = !this->impl->activate_voice(voice);
		}

		bool adsr_finished = false;
		if (state == Disk_Streamer::Voice_State::Started)
		{
			for (int sample = 0; sample < samples.getNumSamples(); ++sample)
			{
				this->impl->adsr_buffer[sample] = this->impl->adsrs[v].getNextSample();
			}
			adsr_finished = !this->impl->adsrs[v].isActive();
		}
		
		if (this->impl->deactivation_pending[v] || adsr_finished)
		{
			this->impl->deactivation_pending[v] = !this->impl->deactivate_voice(voice);
		}
		
		auto sample_id = voice.sample_id;
		auto any_chunk = false;
		for (int channel = 0; channel < samples.getNumChannels(); ++channel)
		{
			if (sample_id == -1)
			{
				continue;
			}
			
			if (!this->impl->disk_streamer.try_get_chunk(&streamed_chunk, v, channel, samples.getNumSamples()))
			{
				continue;
			}
			any_chunk = true;
			
			if (state == Disk_Streamer::Voice_State::Stopping)
			{
				continue;
			}
#if 0
			std::cout << "CHUNK "
					  << sample_id
					  << ": "
					  << this->impl->sample_names[sample_id]
					  << std::endl;
#endif

			float* write = samples.getWritePointer(channel);
			juce::FloatVectorOperations::multiply(streamed_chunk, this->impl->adsr_buffer.data(), samples.getNumSamples());
			juce::FloatVectorOperations::add(write, streamed_chunk, samples.getNumSamples());
		}
		
		if (!any_chunk && state == Disk_Streamer::Voice_State::Stopping)
		{
			if (this->impl->try_deallocate_voice(voice))
			{
#if 0
				std::cout << "Voice: "
						  << voice.voice_id
						  << " -x "
						  << this->impl->sample_names[sample_id]
						  << std::endl;
#endif
				this->impl->disk_streamer.reset_read_head(v);
				this->impl->adsrs[v].reset();
				this->impl->voice_requests[v].store(Disk_Streamer::Voice_State::Stopped);
			}
			else
			{
				std::cout << "Unable to deallocate voice" << std::endl;
			}
		}
	}
}

void Sampler::release()
{
    this->impl->disk_streamer.stop_streaming();
}

static_assert(Sampler::MemoryBytes >= sizeof(Sampler::Impl));
