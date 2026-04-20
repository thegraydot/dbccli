#include "Commands.h"
#include "../core/blob/BlobReader.h"

// Embedded DBD definitions - included only in the CLI binary
#include "../../generated/defs.h"

#include <CLI/CLI.hpp>

int main(int argc, char* argv[]) {
    CLI::App app{"dbccli - World of Warcraft DBC extractor"};
    app.require_subcommand(1);
    RegisterCommands(app);
    CLI11_PARSE(app, argc, argv);

    dbc::BlobReader blob;
    blob.Load(dbc::embedded::BDBC_DATA, dbc::embedded::BDBC_DATA_SIZE);
    return DispatchCommands(blob);
}

