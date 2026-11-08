<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../resources/jusprin/overlay/images/OrcaSlicer_horizontal_dark.svg">
    <img src="../resources/jusprin/overlay/images/OrcaSlicer_horizontal_light.svg" alt="JusPrin" width="240">
  </picture>
</h1>

**Welcome to JusPrin, the first GenAI app for 3D printing!**

Brought to you by the creator of [The Spaghetti Detective](https://www.thespaghettidetective.com/), JusPrin aims to revolutionize the 3D printing experience by eliminating the need for manual slicing. Imagine: **no confusing slicing parameters, no headaches over printing issues caused by incorrect settings. Just print.** That's the promise of JusPrin.

## Table of Contents

- [Why JusPrin?](#why-jusprin)
- [Download](#download)
- [Getting Started](#getting-started)
- [Feedback](#feedback)
- [Contributing](#contributing)
- [License](#license)

## Why JusPrin?

3D printing enthusiasts have relied on slicing tools to prepare models, often struggling with complex parameters and trial-and-error setups. JusPrin changes that by putting AI in charge of the slicing settings. You say what you want the print to do, and AI picks and tunes the settings for your printer and filament.

- **Eliminate Complexity**: No need to tweak dozens of parameters. AI takes care of it.
- **Fix Failed Prints**: Show AI a photo of what went wrong. It reads your file, your settings and your slice, changes the settings and slices again.
- **Stay in Control**: You choose the printer, the filament and what the print is for, and you decide when to print. Every setting is still there when you want it.

JusPrin is available for **Windows**, **macOS** and **Linux**.

## Download

📥 **[Download the Latest Release](https://github.com/TheSpaghettiDetective/JusPrin/releases/latest)**

## Getting Started

1. **Download and open JusPrin.**
2. **Choose how AI runs**: sign in to a JusPrin account, use your own API key, or connect Claude Code or Claude Desktop. You can also just slice for now and set up AI later in Preferences.
3. **Add your printer**: JusPrin can bring in the printers you set up in JusPrin 1 or OrcaSlicer.
4. **Open a model**: STL, OBJ, STEP or 3MF.
5. **Ask AI**: tell it what the print is for, even if you're not sure which settings that needs. It suggests settings from your model and slices.
6. **Print**: send the print to your printer, or export the sliced file.

## Feedback

Your feedback is invaluable! If something goes wrong or a setting doesn't look right, use **Help › Report a problem…** in the app, which opens a GitHub issue with your app version filled in. You can also write to [jusprin-support@obico.io](mailto:jusprin-support@obico.io).

Every bit of feedback helps JusPrin become a better, smarter tool for everyone.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). To report a security problem, see [SECURITY.md](SECURITY.md).

JusPrin 1 lives on the [`jusprin-v1`](https://github.com/TheSpaghettiDetective/JusPrin/tree/jusprin-v1) branch.

## License

JusPrin is open-source software licensed under the GNU Affero General Public License, based on Orca Slicer.

Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca Slicer includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The Bambu networking plugin is based on non-free libraries from BambuLab. It is optional to the Orca Slicer and provides extended functionalities for Bambulab printer users.

**A heartfelt thank you to [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) for providing the foundation on which JusPrin is built!**
