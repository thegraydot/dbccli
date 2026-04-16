CMAKE_BUILD_TYPE := Release
CLANG_VERSION    := 18
WOWDBDEFS_DIR    := extern/WoWDBDefs/definitions
GENERATED_DEFS   := generated/defs.h

DEFGEN  := build/src/defgen/defgen
DBCCLI  := build/src/cli/dbccli

GCC_INSTALL_DIR := $(shell dirname "$(shell gcc -print-libgcc-file-name)")

.PHONY: help \
	build build_debug build_clean build_lint_clean \
	defgen defgen_verbose defgen_debug \
	list_versions list_tables \
	lint_format lint_format_fix lint_cpp lint \
	clean \
	bump_wowdbdefs bump_cli11 bump_submodules

## Show this help menu
help:
	@awk 'BEGIN {FS = ":"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} \
	/^## / {desc = substr($$0, 4); next} \
	/^[a-zA-Z0-9_-]+:/ {if (desc) printf "  \033[36m%-24s\033[0m %s\n", $$1, desc; desc = ""; next} \
	{desc = ""}' $(MAKEFILE_LIST)

# BUILD
## Configure and build (Release)
build:
	cmake -B build -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)
	cmake --build build -j$$(nproc)

## Configure and build (Debug)
build_debug:
	cmake -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build -j$$(nproc)

## Remove cmake build directory
build_clean:
	rm -rf build

## Generate compile_commands.json for clang-tidy
build_lint/compile_commands.json: CMakeLists.txt
	cmake -B build_lint \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_CXX_COMPILER=clang++-$(CLANG_VERSION) \
		-DCMAKE_CXX_FLAGS="--gcc-install-dir=$(GCC_INSTALL_DIR)"

## Remove cmake lint build directory
build_lint_clean:
	rm -rf build_lint

# DEFGEN
## Run defgen standalone - regenerates generated/defs.h
defgen:
	$(DEFGEN) $(WOWDBDEFS_DIR) $(GENERATED_DEFS)

## Run defgen with verbose output (one line per table)
defgen_verbose:
	$(DEFGEN) --verbose $(WOWDBDEFS_DIR) $(GENERATED_DEFS)

## Run defgen with debug output (per-version-block detail)
defgen_debug:
	$(DEFGEN) --debug $(WOWDBDEFS_DIR) $(GENERATED_DEFS)

# DBCCLI
## List all known supported versions
list_versions:
	$(DBCCLI) --list-versions

## List tables supported for a version (usage: make list_tables VERSION=wrath)
list_tables:
	$(DBCCLI) --version $(VERSION) --list-tables

# LINT
## Check C++ formatting with clang-format
lint_format:
	find src \( -name "*.cpp" -o -name "*.h" \) \
	| xargs clang-format-$(CLANG_VERSION) --dry-run --Werror

## Auto-fix C++ formatting with clang-format
lint_format_fix:
	find src \( -name "*.cpp" -o -name "*.h" \) \
	| xargs clang-format-$(CLANG_VERSION) -i

## Run clang-tidy static analysis
lint_cpp: build_lint/compile_commands.json
	clang-tidy-$(CLANG_VERSION) \
	--quiet -p build_lint --header-filter="$(CURDIR)/src/.*" \
	$$(find src -name "*.cpp")

## Run all C++ linters
lint: lint_format lint_cpp

# CLEAN
## Remove all build artifacts
clean: build_clean build_lint_clean

# SUBMODULES
## Bump WoWDBDefs submodule to latest remote
bump_wowdbdefs:
	@read -rp "[*] Bump WoWDBDefs? (y/N) " yn; \
	case $$yn in \
		[yY] ) git submodule update --init --remote extern/WoWDBDefs;; \
		* ) echo "[*] Skipping...";; \
	esac

## Bump CLI11 submodule to latest remote
bump_cli11:
	@read -rp "[*] Bump CLI11? (y/N) " yn; \
	case $$yn in \
		[yY] ) git submodule update --init --remote extern/CLI11;; \
		* ) echo "[*] Skipping...";; \
	esac

## Bump all submodules to latest remote
bump_submodules: bump_wowdbdefs bump_cli11
