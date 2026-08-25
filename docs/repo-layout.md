# Repository Layout

This repository is the Dudu compiler, tooling, docs, examples, and tests. It
should not contain personal scratch projects or generated playground build
output.

## Top Level

- `src/tools/`: tiny executable entry points for `dudu`, `duc`, and `dudu-lsp`.
- `src/dudu/`: shared implementation, split by subsystem:
  `core`, `frontend`, `parser`, `sema`, `codegen`, `native`, `project`, `lsp`,
  `format`, `testing`, and `support`.
- `include/dudu/`: public C/C++ headers exported by Dudu when needed.
- `tests/`: C++ unit tests and `.dd` fixtures for compiler and LSP behavior.
- `examples/`: curated Dudu examples that should stay reproducible.
- `benchmarks/`: generated or curated compiler benchmark inputs.
- `scripts/`: build, test, probe, install, and developer helper scripts.
- `editors/`: editor integrations, including the VS Code extension.
- `docs/`: language, compiler, project-driver, LSP, and planning documents.

The `src/tools/` plus `src/dudu/` split is intentional. Tool entry points should
stay tiny; the actual compiler, project driver, formatter, native scanner, and
language server code lives under `src/dudu/` and is currently built as the
`dudu_frontend` library.

The first subsystem directory split and `dudu-lsp` binary are complete. Future
cleanup should continue from [Repository Refactor Plan](repo-refactor-plan.md),
especially splitting the single implementation library into explicit subsystem
libraries when that pays off.

## What Does Not Belong Here

Do not put personal scratch repos or generated playground output inside this
repo. In particular, `duduplayground` is an external sibling dogfood/scratch
repo when it exists locally, not a subdirectory of this repository.

Dogfood projects such as these should live next to the Dudu repo:

- `/home/vega/Coding/LangDev/Dudu/dogfooding/duduplayground`
- `/home/vega/Coding/LangDev/Dudu/dogfooding/raymarch-dd`
- `/home/vega/Coding/LangDev/Dudu/dogfooding/dudu-webserver`

If a playground case becomes important enough to keep, promote it into one of
the tracked locations:

- `tests/fixtures/` for small deterministic compiler/LSP behavior
- `examples/` for user-facing examples
- `benchmarks/` for performance coverage
- `scripts/probe_optional.sh` or related probe scripts for optional real-library
  compatibility

Generated outputs belong under ignored build/cache directories, not source
directories.

## Local Development Workspace

The compiler is normally developed in a multi-repository workspace rooted at
`/home/vega/Coding/LangDev/Dudu`. Only the repositories are durable; their
parent directories, build trees, downloaded dependencies, and reference clones
can be recreated.

```text
Coding/LangDev/
└── Dudu/
    ├── dudu/                         dudu-language/dudu
    ├── serdde/                       dudu-language/serdde
    ├── dogfooding/
    │   ├── dudu-datascience/         wegfawefgawefg/dudu-datascience
    │   ├── dudu-webserver/           wegfawefgawefg/dudu-webserver
    │   ├── duduplayground/           wegfawefgawefg/duduplayground
    │   └── raymarch-dd/              wegfawefgawefg/raymarch-dd
    └── references/
        ├── julia/                    JuliaLang/julia
        ├── numpy/                    numpy/numpy
        └── pytorch/                  pytorch/pytorch
```

The sibling layout matters to `scripts/test_dogfood.sh`, which defaults to
`../dogfooding`, and to local Serdde/Dudu development workflows. Recreate the
tracked part with:

```sh
workspace_root=/home/vega/Coding/LangDev/Dudu
mkdir -p "$workspace_root/dogfooding" "$workspace_root/references"

git clone https://github.com/dudu-language/dudu.git "$workspace_root/dudu"
git clone https://github.com/dudu-language/serdde.git "$workspace_root/serdde"
git clone https://github.com/wegfawefgawefg/dudu-datascience.git \
    "$workspace_root/dogfooding/dudu-datascience"
git clone https://github.com/wegfawefgawefg/dudu-webserver.git \
    "$workspace_root/dogfooding/dudu-webserver"
git clone https://github.com/wegfawefgawefg/duduplayground.git \
    "$workspace_root/dogfooding/duduplayground"
git clone https://github.com/wegfawefgawefg/raymarch-dd.git \
    "$workspace_root/dogfooding/raymarch-dd"
```

The `references/` checkouts are optional read-only upstream source references,
not Dudu forks or dependencies. They had no local changes and may be omitted or
cloned again when needed:

```sh
git clone https://github.com/JuliaLang/julia.git "$workspace_root/references/julia"
git clone https://github.com/numpy/numpy.git "$workspace_root/references/numpy"
git clone https://github.com/pytorch/pytorch.git "$workspace_root/references/pytorch"
```

Do not back up generated or downloaded trees as project source. In particular,
all Dudu and Serdde `build*` directories, `serdde/build/benchmarks/deps`, and
`duduplayground/third_party` are reproducible. The playground documents its
third-party bootstrap separately. Python virtual environments, Rust `target/`,
editor caches, compiler caches, generated C++, binaries, and benchmark build
artifacts are disposable as well; lockfiles and benchmark reports that are
already tracked remain part of their repositories.
