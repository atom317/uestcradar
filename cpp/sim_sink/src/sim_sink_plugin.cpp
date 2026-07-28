#include "sim_sink.h"

#include <flowgraph/plugin.h>

CY_PLUGIN(
    "sim_sink",
    "1.0.0",
    "Framed IQ validation sink",
    "uestcradar",
    CY_REGISTER_BLOCK_CATEGORY(
        "sim_sink", uestcradar::nodes::SimSink, "sink"))
