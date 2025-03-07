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
- Boost libraries
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

### Building on Linux

Similar to macOS, but you'll need to adjust paths in the Makefile to point to your built JusPrin libraries.

## Usage

```bash
./preset_loader path/to/your/preset_file.json
```

## Output Format

The loader displays:

1. **Preset Information**: Metadata like name, version, and inherits
2. **Additional Metadata**: Setting ID, base ID, etc.
3. **Configuration Values**: All settings in alphabetical order

## Examples

```bash
# Load a printer preset
./preset_loader resources/profiles/PrusaResearch/machine/Original%20Prusa%20i3%20MK3S.json

# Load a filament preset
./preset_loader resources/profiles/BBL/filament/Generic%20ABS.json

# Load a process preset
./preset_loader resources/profiles/BBL/process/0.20mm%20Standard%20@%20BBL%20X1.json
```

## Troubleshooting

### Compilation Issues

- Make sure you have built JusPrin first with `./build_release_macos.sh`
- Check that the library paths in the Makefile point to your actual built libraries
- You may need to adjust include paths in the Makefile based on your system setup

### Runtime Issues

- Ensure the preset file exists and is a valid JSON file
- Runtime errors may indicate issues with the preset structure that would also cause problems in JusPrin