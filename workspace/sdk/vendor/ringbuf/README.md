# SDK-vendored RingBuffer

This directory is the SDK release snapshot of the public RingBuffer ABI. It
allows `workspace/sdk` to configure and build after being copied out of the
monorepo. The canonical implementation lives in `workspace/common/ringbuf`.

When the ABI changes, update this snapshot in the same change and keep its
`ringbuf.hpp`, `ringbuf.cpp`, and `CMakeLists.txt` byte-for-byte equivalent to
the canonical source.
