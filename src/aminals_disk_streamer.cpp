#include <iostream>
#include <optional>
#include <thread>

#include "aminals_disk_streamer.hpp"

using namespace aminals;

struct Disk_Streamer::Impl
{
    static constexpr size_t MemoryBytes = sizeof(std::jthread);
    using Arena = aminals::Arena<MemoryBytes>;    
    Impl() : arena(), thread(std::nullopt)
    {}
    ~Impl() = default;
    
    void start_streaming();
    void stop_streaming();
    Arena arena;
    std::optional<Arena::Ptr<std::jthread>> thread;
};

void Disk_Streamer::Impl::start_streaming()
{
    auto thread_func = [&] (std::stop_token st) {
        while (!st.stop_requested())
        {
            std::cout << "hello thread" << std::endl;
        }
    };
    thread = this->arena.make_unique<std::jthread>(thread_func);
}

void Disk_Streamer::Impl::stop_streaming()
{
    thread->reset();
    this->arena.reset();
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
    this->impl->start_streaming();
    return true;
}

bool Disk_Streamer::try_get_chunk(size_t asset_index, float* out_chunk, size_t chunk_size)
{
    return true;
}

bool Disk_Streamer::stop_streaming()
{
    this->impl->stop_streaming();
    return true;
}

static_assert(Disk_Streamer::MemoryBytes >= sizeof(Disk_Streamer::Impl));
