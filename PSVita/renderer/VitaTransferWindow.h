#pragma once
#include "VitaPlaceTransfer.h"
#include <array>
#include <cstring>

namespace VitaAdhoc { namespace Transfer {
// Fixed storage for the existing chunk protocol. Receiving, retrying
// and draining chunks allocate nothing; the caller owns one window per file.
class Window {
public:
    static constexpr unsigned capacity=256;
    struct Chunk {
        uint64_t position=0;
        size_t count=0;
        bool requested=false, ready=false;
        std::array<unsigned char,chunkSize> bytes;
    };
private:
    std::array<Chunk,capacity> slots;
    Chunk& slot(uint64_t position) {return slots[(position/chunkSize)%capacity];}
public:
    bool contains(uint64_t position) {
        const auto& chunk=slot(position);
        return chunk.position==position && (chunk.requested || chunk.ready);
    }
    bool outstanding(uint64_t position) {
        const auto& chunk=slot(position);
        return chunk.position==position && chunk.requested && !chunk.ready;
    }
    void request(uint64_t position) {
        auto& chunk=slot(position);
        chunk.position=position;chunk.requested=true;chunk.ready=false;chunk.count=0;
    }
    void retry() {for(auto& chunk:slots) if(!chunk.ready) chunk.requested=false;}
    bool accept(uint64_t position,const unsigned char* payload,size_t count) {
        if(count>chunkSize || !outstanding(position)) return false;
        auto& chunk=slot(position);
        if(count) std::memcpy(chunk.bytes.data(),payload,count);
        chunk.count=count;chunk.ready=true;return true;
    }
    const Chunk* next(uint64_t position) {
        const auto& chunk=slot(position);
        return chunk.position==position && chunk.ready ? &chunk : nullptr;
    }
    void consume(uint64_t position) {auto& chunk=slot(position);chunk.requested=chunk.ready=false;chunk.count=0;}
};
} }
