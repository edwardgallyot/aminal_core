#pragma once

#include <cstddef>
#include "aminals_arena.hpp"

namespace aminals
{
    class Disk_Streamer
    {
    public:
        Disk_Streamer();
        ~Disk_Streamer();
        bool start_streaming(double sample_rate, int samples_per_block);
        bool try_get_chunk(float** out_samples, size_t channel, size_t num_samples);
        bool stop_streaming();

        static constexpr size_t MemoryBytes = 4243520;
        using Arena = aminals::Arena<MemoryBytes>;
        struct Impl;
    private:
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
