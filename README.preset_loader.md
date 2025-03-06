# Preset Loader

This is a simple command-line utility for loading JusPrin/OrcaSlicer preset files and displaying their key-value pairs to the console.

## Building and Running the Utility

The preset loader is a standalone application that only depends on the nlohmann/json library. It does not require the full JusPrin/OrcaSlicer build environment.

### Prerequisites

You need:
- A C++ compiler (g++ or clang++)
- The nlohmann/json library

### Step 1: Install the nlohmann/json library

```bash
# On macOS with Homebrew
brew install nlohmann-json

# On Ubuntu/Debian
sudo apt-get install nlohmann-json3-dev
```

### Step 2: Compile the source code

Navigate to the directory containing the `preset_loader_standalone.cpp` file and compile it:

```bash
# Navigate to your JusPrin directory
cd ~/Projects/JusPrin

# Compile the standalone version
g++ -std=c++14 -o preset_loader preset_loader_standalone.cpp -I/opt/homebrew/include
```

This creates an executable file called `preset_loader` in your current directory.

### Step 3: Run the preset loader

```bash
# Basic usage
./preset_loader path/to/your/preset_file.json
```

## Output Format

The preset loader organizes its output into two sections:

1. **Preset Metadata**: Important information about the preset, including:
   - name
   - version
   - inherits (parent preset)
   - setting_id
   - base_id
   - and other metadata fields

2. **Configuration Values**: All the actual settings in the preset, organized alphabetically.

Nested JSON structures (objects and arrays) are properly formatted for readability.

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

If you get compilation errors related to missing headers:

- Check if the nlohmann/json library is properly installed
- Verify the include path in your compilation command:
  - For macOS with Homebrew: `-I/opt/homebrew/include`
  - For Linux/Ubuntu: `-I/usr/include`

If you get runtime errors:

- Make sure the preset file exists and is a valid JSON file
- Check if you have read permissions for the file