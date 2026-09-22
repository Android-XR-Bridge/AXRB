#pragma once

#include "shared_ring.h"
#include "image_frame.h"
#include "windows_gpu_frame.h"
#include "gpu_frame_packet.h"
#include "perf_stats.h"

#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>

namespace axrb::protocol {

// Shared-memory transport for GPU frame metadata.
// Replaces TCP for the descriptor channel, eliminating kernel transitions
// and the ACK round-trip. The shared texture mechanism handles pixel data;
// this ring buffer carries only the metadata (headers + projections + GPU frames).
//
// Protocol:
//   Producer (guest) writes a FrameRingEntry to the ring buffer.
//   Consumer (host) polls the ring buffer and reads entries.
//   No acknowledgment needed - double-buffered shared textures handle sync.
//
// Falls back to TCP if shared memory is unavailable.

constexpr uint32_t kSharedRingNameSize = 64;

// Maximum size of a single frame entry in the ring buffer.
// ImageFrameHeader(64) + ImageProjection(96) + GpuBatchPart(176)*max_layers
// For simplicity, we cap at 16 layers = 64+96+176*16 = 2976 bytes.
// But our ring entry is only 256 bytes. So we need to either:
// 1. Increase ring entry size, or
// 2. Only use ring buffer for single/batch GPU frames (176 bytes)
//
// Option 2 is better: the ring buffer is for the hot path (GPU frames),
// and TCP handles the cold path (pixel fallback, large batches).

// Entry types
constexpr uint8_t kRingEntryGpuFrame = 1;      // Single GPU frame
constexpr uint8_t kRingEntryGpuBatch = 2;       // GPU batch (multiple parts)
constexpr uint8_t kRingEntryEmpty = 3;          // Empty frame (session reset)

struct FrameRingEntry {
    uint8_t type = 0;
    uint8_t reserved[3] = {};
    ImageFrameHeader header;
    ImageProjection projection;
    WindowsGpuFrame gpu;
    // For batch entries, additional parts follow in subsequent ring entries.
    // The batch count is in header.reserved.
};

static_assert(sizeof(FrameRingEntry) <= 256);

class SharedRingTransport {
public:
    SharedRingTransport() = default;

    // Initialize as producer (guest side).
    bool init_producer(const char* name = "/axrb_frame_ring") {
        if (!ring_.create(name)) {
            std::fprintf(stderr, "AXRB Ring: failed to create shared ring '%s'\n", name);
            return false;
        }
        producer_ = true;
        std::fprintf(stderr, "AXRB Ring: producer initialized, capacity=%u\n", kRingCapacity);
        return true;
    }

    // Initialize as consumer (host side).
    bool init_consumer(const char* name = "/axrb_frame_ring") {
        if (!ring_.open(name)) {
            std::fprintf(stderr, "AXRB Ring: failed to open shared ring '%s'\n", name);
            return false;
        }
        producer_ = false;
        std::fprintf(stderr, "AXRB Ring: consumer initialized\n");
        return true;
    }

    // Producer: send a single GPU frame via the ring buffer.
    // Returns true if the entry was written, false if the buffer is full.
    bool send_gpu_frame(const ImageFrameHeader& header,
                       const ImageProjection& projection,
                       const WindowsGpuFrame& gpu) {
        FrameRingEntry entry{};
        entry.type = kRingEntryGpuFrame;
        entry.header = header;
        entry.projection = projection;
        entry.gpu = gpu;
        return ring_.write(&entry, sizeof(entry));
    }

    // Producer: send an empty frame (session reset).
    bool send_empty(uint64_t sequence) {
        FrameRingEntry entry{};
        entry.type = kRingEntryEmpty;
        entry.header.version = kEmptyImageFrameVersion;
        entry.header.sequence = sequence;
        return ring_.write(&entry, sizeof(entry));
    }

    // Producer: send a GPU batch via the ring buffer.
    // Each part is written as a separate entry; the first entry has the batch count.
    bool send_batch(const std::vector<GpuBatchPart>& parts) {
        if (parts.size() < 2) return false;
        // Write the first part with the batch header
        FrameRingEntry entry{};
        entry.type = kRingEntryGpuBatch;
        entry.header = parts[0].header;
        entry.header.version = kGpuBatchFrameVersion;
        entry.header.reserved = static_cast<uint32_t>(parts.size());
        entry.header.sequence = parts.back().header.sequence;
        entry.projection = parts[0].projection;
        entry.gpu = parts[0].gpu;
        if (!ring_.write(&entry, sizeof(entry))) return false;
        // Write remaining parts
        for (uint32_t i = 1; i < parts.size(); ++i) {
            FrameRingEntry part_entry{};
            part_entry.type = kRingEntryGpuBatch;
            part_entry.header = parts[i].header;
            part_entry.projection = parts[i].projection;
            part_entry.gpu = parts[i].gpu;
            if (!ring_.write(&part_entry, sizeof(part_entry))) return false;
        }
        return true;
    }

    // Consumer: receive a frame from the ring buffer.
    // Returns true if an entry was read, false if the buffer is empty.
    // The callback is called with the frame data.
    template<typename Callback>
    bool receive(Callback&& callback) {
        FrameRingEntry entry{};
        if (!ring_.read(&entry, sizeof(entry))) return false;
        if (entry.type == kRingEntryGpuFrame) {
            callback(entry.header, entry.projection, &entry.gpu, sizeof(entry.gpu));
        } else if (entry.type == kRingEntryEmpty) {
            callback(entry.header, entry.projection, nullptr, 0);
        } else if (entry.type == kRingEntryGpuBatch) {
            // Batch: read remaining parts
            const uint32_t count = entry.header.reserved;
            std::vector<uint8_t> batch_data(sizeof(GpuBatchPart) * count);
            GpuBatchPart* parts = reinterpret_cast<GpuBatchPart*>(batch_data.data());
            // First part is already in entry
            parts[0].header = entry.header;
            parts[0].projection = entry.projection;
            parts[0].gpu = entry.gpu;
            // Read remaining parts
            for (uint32_t i = 1; i < count; ++i) {
                FrameRingEntry part_entry{};
                if (!ring_.read(&part_entry, sizeof(part_entry))) return false;
                parts[i].header = part_entry.header;
                parts[i].projection = part_entry.projection;
                parts[i].gpu = part_entry.gpu;
            }
            // Call back with the batch data
            ImageFrameHeader batch_header = entry.header;
            batch_header.payload_size = batch_data.size();
            callback(batch_header, entry.projection, batch_data.data(), batch_data.size());
        }
        return true;
    }

    // Consumer: check if there are entries available.
    bool has_data() const { return ring_.available() > 0; }

    // Consumer: wait for data with timeout.
    template<typename Callback>
    bool wait_receive(Callback&& callback, uint32_t timeout_ms = 1000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (has_data()) return receive(std::forward<Callback>(callback));
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }

    bool is_valid() const { return ring_.is_valid(); }
    bool is_producer() const { return producer_; }

private:
    SharedRingBuffer ring_;
    bool producer_ = false;
};

} // namespace axrb::protocol
