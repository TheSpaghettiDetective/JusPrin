# JusPrin Preset Loader Utility

A command-line utility for loading and examining JusPrin/OrcaSlicer preset files using the actual JusPrin libraries.

## Purpose

This utility exercises the actual JusPrin/OrcaSlicer code to load preset files (Print, Filament, and Printer presets) exactly as the main application would. This ensures that:

1. We're using the real `PresetBundle`, `DynamicPrintConfig`, and other JusPrin classes
2. Any preset compatibility or loading issues will be caught exactly as they would in the application
3. The same code paths are exercised as in the full application

## Building the Preset Loader

The preset loader requires all the dependencies of JusPrin itself, since it uses the actual JusPrin libraries.

### Prerequisites

- A C++ compiler (g++ or clang++)
- Boost libraries (installed via Homebrew on macOS)
- All other JusPrin dependencies
- JusPrin/OrcaSlicer source code with a properly built `liblibslic3r.a` library

### Building on macOS

```bash
cd ~/Projects/JusPrin

# First ensure JusPrin libraries are built
./build_release_macos.sh

# Then build the preset loader
make -f Makefile.preset_loader
```

This will compile `main.cpp` with the correct libraries and include paths. The Makefile is configured to:

- Link against the required Boost libraries in the correct order
- Include all necessary JusPrin libraries
- Set the correct include paths for Boost and other dependencies
- Properly handle dynamic linking with the Boost.Log library

### Troubleshooting Build Issues

If you encounter linking errors, particularly related to Boost libraries:

1. Ensure Boost is properly installed: `brew install boost`
2. Check that the Boost library path in the Makefile (`BOOST_LIB_DIR`) points to your Boost installation
3. Verify that you've built JusPrin first with `./build_release_macos.sh`
4. Make sure all the libraries in `build_arm64/src/` are present

## Usage

```bash
./preset_loader path/to/your/preset_file.json
```

### Examples

```bash
# Load a printer preset
./preset_loader resources/profiles/Wanhao/machine/fdm_wanhao_common.json

# Load a filament preset (if available)
./preset_loader resources/profiles/BBL/filament/Generic%20PLA.json

# Load a process preset (if available)
./preset_loader resources/profiles/BBL/process/0.20mm%20Standard%20@%20BBL%20X1.json
```

## Output Format

The loader displays:

1. **Preset Information**: Metadata like name, version, and inherits
2. **Additional Metadata**: Setting ID, base ID, etc.
3. **Configuration Values**: All settings in alphabetical order

## Implementation Details

The preset loader is implemented in `main.cpp` and uses the actual JusPrin libraries to load and parse preset files. This ensures full compatibility with the main application. The implementation:

1. Initializes the JusPrin configuration system
2. Loads the specified preset file
3. Displays the preset information in a readable format
4. Uses the same validation and parsing logic as the main application

## Cleaning Up

To clean the built files:

```bash
make -f Makefile.preset_loader clean
```