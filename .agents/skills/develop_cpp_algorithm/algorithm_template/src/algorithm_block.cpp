#include "algorithm.h"

#include <cycore_algorithm_sdk.h>

CYCORE_EXPORT_ALGORITHM(
    "my_plugin",
    "algorithm.my_block",
    MyAlgorithm,
    cycore::algorithm::my_block::InputSample,
    cycore::algorithm::my_block::OutputSample
)
