# FileSource → SimSink integration

该目录不编译两个 Block 的源码，只动态加载分别构建的插件：

```bash
cmake -S cpp/file_source -B /tmp/build-file-source
cmake --build /tmp/build-file-source -j

cmake -S cpp/sim_sink -B /tmp/build-sim-sink
cmake --build /tmp/build-sim-sink -j

cmake -S cpp/file_source_sim_sink_integration \
  -B /tmp/build-file-source-sim-sink \
  -DFILE_SOURCE_PLUGIN=/tmp/build-file-source/file_source.so \
  -DSIM_SINK_PLUGIN=/tmp/build-sim-sink/sim_sink.so
cmake --build /tmp/build-file-source-sim-sink -j
ctest --test-dir /tmp/build-file-source-sim-sink --output-on-failure
```
