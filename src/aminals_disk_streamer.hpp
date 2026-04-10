#pragma once

#include <cstddef>
#include "aminals_arena.hpp"

namespace aminals
{
    class Disk_Streamer
    {
    public:
		static constexpr size_t Num_Voices = 32;
		static constexpr size_t Channels_Per_Voice = 2;

		Disk_Streamer(std::array<std::atomic_bool, Num_Voices>& voice_requests,
					  std::array<std::atomic<long long>, Num_Voices>& voice_request_sample_ids);
        ~Disk_Streamer();

		void set_streaming_file(const char* filename);
		void set_total_sizes(const unsigned long long* sizes, size_t count);
		void set_offsets(const unsigned long long* offsets, size_t count);
		void set_channel_strides(const unsigned long long* channel_strides, size_t count);
		void set_sample_counts(unsigned long long* sample_counts, size_t count);
		bool start_streaming(double sample_rate, int samples_per_block);
        bool try_get_chunk(float** out_samples, long long voice_id, size_t channel, size_t num_samples);
        bool stop_streaming();

        static constexpr size_t MemoryBytes = Arena<>::const_align(50596000, sizeof(size_t));
        using Arena = aminals::Arena<MemoryBytes>;
        struct Impl;
    private:
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
