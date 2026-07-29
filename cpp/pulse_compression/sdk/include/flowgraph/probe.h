#ifndef CYCORE_FLOWGRAPH_PROBE_H
#define CYCORE_FLOWGRAPH_PROBE_H

#include "data_type_traits.h"

#include <common/i_probe.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cy::flowgraph {

namespace detail {

constexpr std::size_t kDefaultProbeMaxElements = 4096;
constexpr std::size_t kDefaultProbeMaxBytes = 64 * 1024;

} // namespace detail

template <typename T>
class Probe final : public cy::common::IProbe {
public:
    // frame_size is an exact observation window measured in elements of T.
    // Commit boundaries may split a window; snapshots become visible only
    // after a complete, stream-aligned window has been collected.
    Probe(std::string topic,
          std::size_t frame_size,
          std::size_t max_elements = detail::kDefaultProbeMaxElements,
          std::size_t max_bytes = detail::kDefaultProbeMaxBytes)
        : topic_(std::move(topic)),
          frame_size_(std::min(frame_size == 0 ? detail::kDefaultProbeMaxElements : frame_size,
                               max_elements)),
          max_bytes_(std::min(max_bytes, frame_size_ * sizeof(T))),
          staging_(max_bytes_),
          snapshot_(max_bytes_) {
        if (topic_.empty()) {
            throw std::invalid_argument("probe topic must not be empty");
        }
        if (frame_size_ == 0 || max_bytes_ != frame_size_ * sizeof(T)) {
            throw std::invalid_argument(
                "probe limits must hold one complete frame_size window");
        }
    }

    const std::string& topic() const noexcept override { return topic_; }
    cy::common::DataType data_type() const noexcept override { return DataTypeTraits<T>::value; }
    std::size_t element_size() const noexcept override { return sizeof(T); }
    std::size_t frame_size() const noexcept override { return frame_size_; }

    void request() noexcept override {
        request_pending_.store(true, std::memory_order_release);
    }

    std::size_t peek_latest(cy::common::Span<std::byte> buffer) const override {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (snapshot_size_ != max_bytes_ || buffer.size() < snapshot_size_) {
            return 0;
        }
        std::memcpy(buffer.data(), snapshot_.data(), snapshot_size_);
        return snapshot_size_;
    }

    void capture_latest(const T* data, std::size_t count) noexcept {
        if (data == nullptr || count == 0) {
            return;
        }

        std::size_t offset = 0;
        while (offset < count) {
            if (!collecting_) {
                if (!request_pending_.load(std::memory_order_acquire)) {
                    advance_stream_position(count - offset);
                    return;
                }

                if (stream_position_ != 0) {
                    const std::size_t skip =
                        std::min(count - offset, frame_size_ - stream_position_);
                    offset += skip;
                    advance_stream_position(skip);
                    if (offset == count) {
                        return;
                    }
                }

                bool expected = true;
                if (!request_pending_.compare_exchange_strong(
                        expected, false,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    advance_stream_position(count - offset);
                    return;
                }
                collecting_ = true;
                staging_size_ = 0;
            }

            const std::size_t elements = std::min(
                count - offset, frame_size_ - staging_size_);
            std::memcpy(
                staging_.data() + staging_size_ * sizeof(T),
                data + offset,
                elements * sizeof(T));
            staging_size_ += elements;
            offset += elements;
            advance_stream_position(elements);

            if (staging_size_ == frame_size_) {
                publish_staging();
                collecting_ = false;
                staging_size_ = 0;
            }
        }
    }

    static void capture_latest_hook(void* probe, const T* data, std::size_t count) noexcept {
        if (!probe) {
            return;
        }
        static_cast<Probe<T>*>(probe)->capture_latest(data, count);
    }

private:
    void advance_stream_position(std::size_t count) noexcept {
        const std::size_t remaining = frame_size_ - stream_position_;
        if (count < remaining) {
            stream_position_ += count;
            return;
        }
        stream_position_ = (count - remaining) % frame_size_;
    }

    void publish_staging() noexcept {
        if (!snapshot_mutex_.try_lock()) {
            request_pending_.store(true, std::memory_order_release);
            return;
        }

        std::unique_lock<std::mutex> lock(snapshot_mutex_, std::adopt_lock);
        std::memcpy(snapshot_.data(), staging_.data(), max_bytes_);
        snapshot_size_ = max_bytes_;
    }

    std::string topic_;
    std::size_t frame_size_ = 0;
    std::size_t max_bytes_ = 0;

    std::atomic<bool> request_pending_{false};
    std::vector<std::byte> staging_;
    std::size_t stream_position_ = 0;
    std::size_t staging_size_ = 0;
    bool collecting_ = false;

    mutable std::mutex snapshot_mutex_;
    std::vector<std::byte> snapshot_;
    std::size_t snapshot_size_ = 0;
};

class ProbeRegistry final : public cy::common::IProbeProvider {
public:
    void AddProbe(std::shared_ptr<cy::common::IProbe> probe,
                  cycore::du::common::StreamMetadataPtr metadata = {}) {
        if (!probe) {
            throw std::invalid_argument("probe registry cannot add null probe");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string topic = probe->topic();
        if (!probes_.emplace(topic, std::move(probe)).second) {
            throw std::invalid_argument("duplicate probe topic: " + topic);
        }
        if (metadata) {
            metadata_.emplace(topic, std::move(metadata));
        }
    }

    std::shared_ptr<cy::common::IProbe> GetProbe(const std::string& topic) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = probes_.find(topic);
        if (it == probes_.end()) {
            return {};
        }
        return it->second;
    }

    std::vector<std::string> ListProbeTopics() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> topics;
        topics.reserve(probes_.size());
        for (const auto& item : probes_) {
            topics.push_back(item.first);
        }
        return topics;
    }

    cycore::du::common::StreamMetadataPtr GetProbeMetadata(const std::string& topic) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = metadata_.find(topic);
        if (it == metadata_.end()) {
            return {};
        }
        return it->second;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<cy::common::IProbe>> probes_;
    std::unordered_map<std::string, cycore::du::common::StreamMetadataPtr> metadata_;
};

} // namespace cy::flowgraph

#endif // CYCORE_FLOWGRAPH_PROBE_H
