#pragma once

#include "../core/blob/BlobReader.h"
#include <CLI/CLI.hpp>

// Register all subcommands and their options onto app.
// Must be called before CLI11_PARSE.
void RegisterCommands(CLI::App& app);

// Dispatch to the subcommand that was parsed.
// blob must already be loaded with the embedded definitions.
int DispatchCommands(dbc::BlobReader& blob);
