#pragma once
#include <cstddef>
#include "VitaHttpRequestPolicy.h"
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace RBX {
// Curl writes into the eventual result storage. Publishing transfers ownership
// instead of retaining a stringstream, full-size vector and result at once.
class VitaHttpResponseBuffer {
    std::string payload;
    size_t maxBytes;
public:
    std::size_t bytes = 0;
    bool limitExceeded = false;
    explicit VitaHttpResponseBuffer(size_t limit = vitaHttpRequestPolicy().maxBytes) : maxBytes(limit) {}
    static bool writeSize(std::size_t size, std::size_t count, std::size_t& result) noexcept {
        if (size && count > (std::numeric_limits<std::size_t>::max)()/size) return false;
        result = size*count;
        return true;
    }
    std::size_t append(const char* data, std::size_t size, std::size_t count) noexcept {
        std::size_t length;
        if (!writeSize(size,count,length) || !length || !data ||
            length > payload.max_size()-payload.size()) return 0;
        if (payload.size() > maxBytes || length > maxBytes - payload.size()) {
            limitExceeded = true;
            return 0;
        }
        // Exceptions must not cross libcurl's C callback boundary. String append
        // retains the old bytes on failure; returning zero aborts the transfer.
        try { payload.append(data,length); }
        catch (const std::bad_alloc&) { return 0; }
        catch (const std::length_error&) { return 0; }
        bytes = payload.size();
        return length;
    }
    void assignCached(const char* data, std::size_t length) {
        if (length > maxBytes) throw std::length_error("HTTP response exceeds the Vita request limit");
        if (length) payload.assign(data,length);
        else payload.clear();
        bytes = length;
    }
    void publish(std::string& output) noexcept {
        output.swap(payload);
        // Release any former result promptly, before cache processing begins.
        std::string().swap(payload);
        // Keep the received count for the existing post-transfer statistics.
    }
    void reset() {
        payload.clear();
        bytes = 0;
        limitExceeded = false;
    }
};
}
