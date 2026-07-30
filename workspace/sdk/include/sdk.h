#pragma once

#include <data.h>

#include <cstddef>
#include <utility>

#if defined(_WIN32)
#  if defined(CYCOMM_SDK_BUILD)
#    define CYCOMM_SDK_API __declspec(dllexport)
#  else
#    define CYCOMM_SDK_API __declspec(dllimport)
#  endif
#else
#  define CYCOMM_SDK_API __attribute__((visibility("default")))
#endif

namespace uestcradar {

template <class Frame>
class FrameHandle;

namespace detail {

struct PortState;
struct FrameAccess {
    template <class Frame>
    static FrameHandle<Frame> make(
        Frame frame,
        PortState* state) noexcept;

    template <class Frame>
    static PortState* state(FrameHandle<Frame>& frame) noexcept;

    template <class Frame>
    static void finish(FrameHandle<Frame>& frame) noexcept;
};

CYCOMM_SDK_API void retain(PortState* state) noexcept;
CYCOMM_SDK_API void abandon(PortState* state) noexcept;
CYCOMM_SDK_API void release(PortState* state) noexcept;

}  // namespace detail

template <class Frame>
class FrameHandle final : public Frame {
public:
    FrameHandle(FrameHandle&& other) noexcept
        : Frame(std::move(other)),
          state_(std::exchange(other.state_, nullptr)) {}

    FrameHandle& operator=(FrameHandle&&) = delete;
    FrameHandle(const FrameHandle&) = delete;
    FrameHandle& operator=(const FrameHandle&) = delete;

    ~FrameHandle() {
        if (state_ != nullptr) {
            detail::abandon(state_);
            detail::release(state_);
        }
    }

private:
    FrameHandle(Frame frame, detail::PortState* state) noexcept
        : Frame(std::move(frame)), state_(state) {
        detail::retain(state_);
    }

    void finish() noexcept {
        detail::release(state_);
        state_ = nullptr;
    }

    detail::PortState* state_{nullptr};

    friend struct detail::FrameAccess;
    template <class>
    friend class Input;
    template <class>
    friend class Output;
};

template <class Frame>
class CYCOMM_SDK_API Input {
public:
    Input();
    Input(Input&& other) noexcept;
    Input& operator=(Input&& other) noexcept;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    ~Input();

    [[nodiscard]] FrameHandle<Frame> read();

private:
    detail::PortState* state_{nullptr};
};

template <class Frame>
class CYCOMM_SDK_API Output {
public:
    Output();
    Output(Output&& other) noexcept;
    Output& operator=(Output&& other) noexcept;
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output();

    [[nodiscard]] FrameHandle<Frame> create(
        const typename Frame::Metadata& metadata);
    void write(FrameHandle<Frame>& frame);

private:
    detail::PortState* state_{nullptr};
};

extern template class Input<IQFrame>;
extern template class Input<PulseCompressionFrame>;
extern template class Input<RDFrame>;
extern template class Output<IQFrame>;
extern template class Output<PulseCompressionFrame>;
extern template class Output<RDFrame>;

}  // namespace uestcradar
