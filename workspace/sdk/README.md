# Cycomm SDK

The SDK is self-contained: copy this directory and build it without the
Sidecar or the rest of the repository.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix /desired/prefix
```

`vendor/ringbuf` is a statically linked release snapshot of the public shared
memory ABI. It has no runtime library dependency after `libuestcradar_sdk.so`
is built. The canonical ABI source is maintained at
`workspace/common/ringbuf`; any ABI change must update the vendor snapshot in
the same commit.
