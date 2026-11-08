# Contributing to JusPrin

Thanks for wanting to help!

## Reporting a problem

- In the app, use **Help › Report a problem…**. It opens a GitHub issue with your app version and system filled in.
- Or open an issue with the bug report form. Attach the project file and the log folder; the form says where to find them.
- Search the existing issues first. If yours is there, add a comment instead.
- For a security problem, don't open an issue. Follow [SECURITY.md](SECURITY.md).
- For questions, write to [jusprin-support@obico.io](mailto:jusprin-support@obico.io).

JusPrin is built on [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer). If a slicing problem also happens in OrcaSlicer, report it to OrcaSlicer as well, since a fix there reaches JusPrin with the next OrcaSlicer release JusPrin takes.

## Pull requests

- Open pull requests against `jusprin-dev`, the development branch.
- Open an issue first for anything larger than a small fix, so we can agree on the approach.
- Keep each pull request to one topic.
- Explain why the change is needed and how you tested it. Add an automated test when the code can be tested.
- JusPrin's own code lives in `src/slic3r/GUI/JusPrin/`. Changes to OrcaSlicer's files must be small, because each one has to be carried forward whenever JusPrin takes a new OrcaSlicer release.
- AI tools are welcome. You are still responsible for the change, so test it and make sure you understand it.

## Building

Follow OrcaSlicer's [How to build](https://www.orcaslicer.com/wiki/how_to_build) guide. JusPrin also needs Node.js and npm to build its AI and Home pages.

## License

By contributing, you agree that your contribution is licensed under the GNU Affero General Public License, version 3, like the rest of JusPrin.
