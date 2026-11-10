# Contributing to Pullform

Thanks for your interest in improving Pullform.

## Getting set up

1. Install Node.js 22.12 or newer and Git.
2. Clone the repository and install dependencies:
   ```sh
   git clone https://github.com/anyday901/pullform.git
   cd pullform
   npm ci
   ```
3. Start the dev server with `npm run dev` and open http://localhost:5173.

## Before opening a pull request

- Run `npm run check` (type checks, unit tests and production build).
- Run `npm run test:e2e` for changes that touch the interface, tools or viewport.
- Add or update tests for behavior you change. Geometry and file-format changes need unit
  tests.
- Update `CHANGELOG.md` under **Unreleased**, and the README or docs if behavior or
  controls change.

## Ground rules

- Every model change goes through `Editor.commit()`, so it is validated and undoable.
  Previews must never modify the document or history.
- Keep modeling logic (`src/core`, `src/model`, `src/geometry`, `src/history`) free of
  React and Three.js.
- Store lengths in meters; convert units only at input and display.
- Changing the project file format requires a new format version and a migration from the
  previous one.
- Use original code and assets, and only dependencies with permissive open-source
  licenses. Record new bundled dependencies in `THIRD_PARTY_NOTICES.md`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit together and
[docs/ROADMAP.md](docs/ROADMAP.md) for planned work.

By contributing, you agree that your contributions are licensed under the MIT License.
