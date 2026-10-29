#pragma once
#include "Builder.h"
#include "Json.h"
namespace ii::builder::detail {
bool extended_target(const Kit &);
bool adapter_target(const Kit &);
bool hardware_target(const Kit &);
TargetSettings read_target(const Json &, const std::filesystem::path &);
Json target_json(const TargetSettings &);
void validate_target(Kit &, const Request &);
std::string target_preflight(Kit &);
std::string target_system(const Kit &, bool host_validation = false);
ProjectPlan adapter_plan(const Request &, const KitPlan &);
core::Result<void> prepare_adapter(const ProjectPlan &, const Kit &, const Request &);
std::vector<std::filesystem::path> adapter_artifacts(const ProjectPlan &, const Kit &);
std::vector<Command> after_commands(const Request &, const KitPlan &, Phase);
} // namespace ii::builder::detail
