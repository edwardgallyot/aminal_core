#include <thread>

#include "aminals_disk_streamer.hpp"


using namespace aminals;


struct Disk_Streamer::Impl
{
    Impl() {}
    ~Impl() {}
    
    std::thread thread;
};

Disk_Streamer::Disk_Streamer()
    : arena(),
      impl(arena.make_unique<Disk_Streamer::Impl>())
{
}

Disk_Streamer::~Disk_Streamer()
{
}

bool Disk_Streamer::start_streaming()
{
    return true;
}

bool Disk_Streamer::try_get_chunk(size_t asset_index, float* out_chunk, size_t chunk_size)
{
    return true;
}

bool Disk_Streamer::stop_streaming()
{
    return true;
}
