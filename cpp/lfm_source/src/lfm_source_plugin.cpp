#include <flowgraph/block_wrapper.h>
#include <flowgraph/plugin.h>
#include <flowgraph/blocks/common/lfm_source.h>

CY_PLUGIN("lfm_source_plugin", "1.0.0", "LFM Source generator test plugin", "cycore",
          plugin.block_registry().register_context_factory(
              "lfm_source",
              [](std::string instance_name,
                 const cy::flowgraph::ValueMap& params,
                 cy::common::IExecutionContext*) -> std::unique_ptr<cy::flowgraph::BlockModel> {
                  return std::make_unique<cy::flowgraph::BlockWrapper<
                      cy::flowgraph::blocks::common::LFMSource>>(
                          std::move(instance_name),
                          cy::flowgraph::BlockTypeName{"lfm_source"},
                          params);
              },
              ::cy::flowgraph::BlockMetadata{"lfm_source", "lfm_source", "", "source"});)
