#include "file_source.h"

#include <flowgraph/plugin.h>

CY_PLUGIN(
    "file_source",
    "1.0.0",
    "Looping framed IQ file source",
    "uestcradar",
    CY_REGISTER_BLOCK_CATEGORY(
        "file_source", uestcradar::nodes::FileSource, "source"))
