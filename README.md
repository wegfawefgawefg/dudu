# dudu

[Website](https://dudulang.org) | [Install](docs/installing.md) |
[Language Guide](docs/language.md) | [Performance](docs/performance.md) |
[Known Limitations](docs/known-limitations.md)

Dudu is a statically typed Python-shaped systems language that compiles to
readable C++20.

The target is simple: write code that looks close to Python, keep C/C++ style
control over types and memory, use existing C and C++ libraries directly, and
produce native code that C++ projects can consume.

Source files use `.dd`.

```python
from c import raylib.h as rl


class Player:
    hp: i32
    x: f32
    y: f32


def main() -> i32:
    player = Player(hp=100, x=400.0, y=300.0)

    rl.InitWindow(800, 600, "dudu")

    while not rl.WindowShouldClose():
        rl.BeginDrawing()
        rl.ClearBackground(rl.BLACK)
        rl.DrawCircle(i32(player.x), i32(player.y), 40.0, rl.RED)
        rl.EndDrawing()

    rl.CloseWindow()
    return 0
```

## Status

Dudu is usable as an alpha compiler. It can parse and check the core typed
subset, format source, compile multi-file projects, import native C/C++
headers, emit readable C++20, and drive CMake-backed builds through `dudu`.

It is not a stable language release. The current release is
`0.1.0-alpha.13`. Language and generated ABI compatibility may change between alpha versions.
See [Known Limitations](docs/known-limitations.md) before adopting it.

Current language coverage includes Dudu-native generics, payload enums with
exhaustive `match`, fixed arrays with matrix/tensor-style indexing and slices,
operator overloads, native inheritance, typed additive declaration macros,
generated CMake module builds, and initial LSP support. Project tooling includes
path/Git Dudu source dependencies with a lockfile, so early Dudu packages can be
consumed before a central package registry exists.

User macros use a public typed declaration AST rather than source strings or
token substitution. See [User-Defined Macros](docs/macros.md), and inspect a
project's generated declarations with:

```sh
duc expand src/main.dd --show-origins
```

## Install

See [Installing Dudu](docs/installing.md) for every planned release channel,
ownership rules, and validation status.

The primary installer builds the immutable tagged source archive locally,
offers to install missing native dependencies, verifies its SHA-256 checksum,
and installs an atomic user-local toolchain:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://dudulang.org/install.sh | sh
dudu --version
```

Installer-owned toolchains update and roll back without replacing the active
compiler until the new build passes its smoke check:

```sh
dudu update --check
dudu update
dudu update --rollback
dudu uninstall
```

Homebrew, AUR, and `.deb` installations reject `dudu update` and direct users
back to the package manager that owns their files.

### Install From A Checkout

Clone, build, and install:

```sh
git clone https://github.com/dudu-language/dudu.git
cd dudu
./scripts/install-local.sh
export PATH="$HOME/.local/bin:$PATH"
dudu --version
```

`install-local.sh` installs:

- `dudu`, the project driver
- `duc`, the lower-level compiler driver
- `dudu-lsp`, the language server used by editor integrations
- docs
- editor support files

The default install prefix is `~/.local`. Use another prefix with:

```sh
./scripts/install-local.sh --prefix /path/to/prefix
```

Update this checkout-owned installation with:

```sh
git pull --ff-only
./scripts/install-local.sh
```

Uninstall the default checkout-owned installation explicitly:

```sh
rm -f "$HOME/.local/bin/dudu" "$HOME/.local/bin/duc" "$HOME/.local/bin/dudu-lsp"
rm -rf "$HOME/.local/share/dudu" "$HOME/.local/share/doc/dudu"
```

For a custom prefix, replace `$HOME/.local` with that prefix. Do not use this
manual removal for a future package-manager-owned installation.

## First Project

```sh
dudu init hello
cd hello
dudu run
```

Common project commands:

```sh
dudu check
dudu build
dudu run
dudu test
dudu fmt
dudu clean
```

Add `--timings` when a build feels slow:

```sh
dudu run --timings
```

## Project Files

Simple projects use `dudu.toml`:

```toml
name = "hello"
entry = "src/main.dd"

[cxx]
standard = "c++20"

[pkg]
libs = ["raylib"]
```

For larger native projects, `dudu build`, `dudu run`, and `dudu test` stay the
front door, but CMake is the serious native backend. Dudu can generate and drive
internal CMake projects, and it can also build user-owned CMake projects when
the native build needs toolchain files, platform rules, vendored C/C++ code, or
larger dependency graphs.

Generated CMake under `build/` belongs to Dudu and may be overwritten.
User-owned `CMakeLists.txt` files are never patched by Dudu. New projects get
a starter `CMakeLists.txt` that builds generated Dudu module artifacts through
`duc`; it discovers generated module `.cpp` files so simple multi-file Dudu
projects link correctly. After creation, that file belongs to the project.

## Native Interop

Import C and C++ headers directly:

```python
from c import raylib.h as rl
from cpp import vector
```

C++ standard library headers expose their normal namespaces, so `from cpp
import vector` makes `std.vector` available.

Use Dudu source dependencies through `dudu.toml`:

```toml
[deps]
local_math = { path = "../local_math" }
ndad = { git = "https://github.com/wegfawefgawefg/ndad.git", tag = "v0.1.0" }
```

Fetch or refresh missing source dependencies with:

```sh
dudu deps fetch
```

Path and Git dependencies must resolve to a Dudu package root containing
`dudu.toml`; if a package has `src/`, that directory is used as its module root.

Native C/C++ libraries still belong to CMake, pkg-config, system packages, or
vendored native code. Dudu source dependencies are for `.dd` packages.

Generated headers are available for C and C++ integration:

```sh
duc emit examples/cpp_library.dd -o build/cpp_library.cpp
duc examples/cpp_library.dd --emit-header build/cpp_library.hpp
```

Optional examples that use native libraries such as raylib, SDL3, OpenCV,
Vulkan, or FFmpeg need those libraries installed separately.

External dogfood projects can be checked locally with:

```sh
./scripts/test_examples.sh
./scripts/test_dependencies.sh
./scripts/test_dogfood.sh
```

The example script skips missing optional package SDKs. The dogfood script
skips missing local repos and currently covers `raymarch-dd`,
`dudu-webserver`, and `dudu-datascience` when they exist next to the Dudu
checkout. The full validation matrix lives in
[`docs/validation-matrix.md`](docs/validation-matrix.md). The local multi-repo
layout, optional reference clones, and recreation commands are documented in
[`docs/repo-layout.md`](docs/repo-layout.md#local-development-workspace).

## Editor Support

Editor files live in:

- `editors/vscode`
- `editors/vim`
- `editors/nvim`

The VS Code folder contains the production extension with `.dd` highlighting, LSP-backed
diagnostics/navigation/hover/inlay hints, and command palette actions for
formatting, checking, building, and running Dudu files. Build and clean-install
the prerelease VSIX with:

```sh
./scripts/test-vscode-package.sh
```

Until the prerelease extension is published, launch it from a checkout:

```sh
cd editors/vscode
npm ci
code --extensionDevelopmentPath="$PWD"
```

The language server starts with:

```sh
dudu-lsp
```

## Roadmap

- [x] Parse and compile a typed Python-shaped subset.
- [x] Emit readable C++20.
- [x] Drive project builds with `dudu`.
- [x] Compile multi-file Dudu projects.
- [x] Import C/C++ headers through clang-based native scanning.
- [x] Use generated CMake as the normal project backend.
- [x] Install from a checkout with `scripts/install-local.sh`.
- [x] Add native generics, payload enums, fixed arrays, slicing, operator
      overloads, and inheritance.
- [x] Emit separate generated files through the generated-CMake backend.
- [x] Add path/Git Dudu source dependencies and stable lockfile generation.
- [x] Add shaped generics and library-owned indexing hooks for tensor-style
      APIs without tensor package special cases in the compiler.
- [x] Complete the core AST/destringing migration for normal Dudu statements,
      expressions, types, sema, codegen, and lint paths.
- [x] Keep module imports canonical so the same `.dd` file reached through
      multiple import routes is one module, not duplicate declarations.
- [ ] Add module-level compiler invalidation for faster Dudu-side rebuilds.
- [ ] Harden native interop against common C++ libraries.
- [ ] Keep expanding public dogfood projects and optional native/library
      compatibility probes.
- [ ] Finish LSP hover, go-to-definition, references, diagnostics, and formatter
      support on top of the real AST.
- [ ] Add a broad compatibility suite for real libraries and larger examples.
- [x] Add tagged source artifacts, the source bootstrap installer, atomic
      update/rollback/uninstall, a production VSIX, AUR/Homebrew recipes, and a
      downloadable `.deb` build.
- [x] Publish the first Linux alpha, package recipes, website, and editor
      extension.
- [ ] Validate the complete tagged source lifecycle on a maintained Apple
      Silicon machine; macOS remains a best-effort source build meanwhile.

The pre-alpha release gate and distribution sequence are documented in
[`docs/distribution-plan.md`](docs/distribution-plan.md). The exact local gate
is:

```sh
./scripts/release-check.sh
```

The active roadmap lives in [`docs/leplan2.md`](docs/leplan2.md). The detailed
implementation history and subsystem index remain in
[`docs/le_plan.md`](docs/le_plan.md).
Developer command details live in [`docs/developer-guide.md`](docs/developer-guide.md).

## License

Dudu is licensed under either the
[Apache License 2.0](LICENSE-APACHE) or the [MIT License](LICENSE-MIT), at your
option. Contributions are accepted under the same terms unless explicitly
stated otherwise.

Dudu does not claim copyright over programs written by users or over generated
C/C++ program output. Dudu-owned runtime, prelude, or support code included in
generated output remains available under the same dual license. See
[`COPYRIGHT`](COPYRIGHT) for the complete project statement.
