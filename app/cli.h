#pragma once

#include <string>

struct AppConfig;

/// Parse command-line arguments into \p config.
/// Returns true to continue, false to exit (help/completion was printed).
bool parse_cli(int argc, char** argv, AppConfig& config, std::string& config_path);
