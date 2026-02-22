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
        bool start_streaming();
        bool try_get_chunk(size_t asset_index, float* out_chunk, size_t chunk_size);
        bool stop_streaming();

        // TODO(edg): Let's have a think about how big the streamer
        // should be.
        static constexpr size_t MemoryBytes = 1024;
        using Arena = aminals::Arena<MemoryBytes>;
    private:
        struct Impl;
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
