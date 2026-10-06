# Contributing to Upipe

## Issues and pull requests

Upipe is developed on GitHub at <https://github.com/Upipe/upipe>.

- Report bugs and request features in the
  [issue tracker](https://github.com/Upipe/upipe/issues). For bugs, give the
  Upipe commit, platform and compiler, and a minimal way to reproduce
  (pipeline, command line, sample stream if possible).
- Submit changes as pull requests against `master`. Keep each pull request
  focused on one topic, and rebase it on `master` rather than merging `master`
  into it.
- Each commit should be one self-contained logical change that builds and
  passes the test suite on its own. Squash `fixup!` and work-in-progress
  commits before asking for a merge.
- The [CI](.github/workflows/test-suite.yaml) builds and runs the test suite on
  Linux (gcc, clang, cross-compiled armhf/arm64, static) and macOS; it must
  pass before a pull request is merged.

## Commit messages

Follow the 50/72 rule: a summary line of about 50 characters or less, a blank
line, then a body wrapped at 72 columns.

Prefix the summary with the abbreviated name of the pipe or component it
touches, followed by a colon and an imperative description:

    upipe_avcenc: set CPB attributes in flow def
    upipe_ffmt: add support for software tone mapping
    upipe_ts_demux: fix compilation without EMM support

When the change is not about a single pipe, use the relevant area instead
(`build`, `tests`, `ci`, `doc`, `upump_ev`, `luajit`, ...).

Use the body to explain *why* the change is needed, concisely: the bug and its
consequence, or the motivation for the feature. The diff already says what was
changed. Trivial commits may have no body.

## Building and testing

The build system is plain GNU Make with a `configure` script; it supports
out-of-tree builds:

    mkdir build && cd build
    ../configure --prefix=$HOME/upipe
    make -j$(nproc)
    make -j$(nproc) check

`make help` lists the targets and the configuration options. Sanitizers can be
enabled with `--enable-asan`, `--enable-ubsan`, `--enable-tsan` or
`--enable-lsan`; otherwise the tests run under valgrind, which must then be
installed.

New pipes should come with a unit test in `tests/`, registered in
`tests/Build.mk`. New sources and headers must be added to the `Build.mk` of
their library.

## Coding style

Match the code around you. In short:

- C17, 4-space indentation, no tabs, no trailing whitespace, lines of at most
  80 columns.
- K&R braces: the opening brace of a function goes on its own line, the one of
  `if`/`for`/`while`/`switch`/`struct` stays on the same line. Braces are
  omitted around single-statement bodies. `case` labels are indented inside
  `switch`.
- Continuation lines are aligned with the opening parenthesis.
- Lowercase `snake_case` identifiers; every public symbol is prefixed by its
  module (`upipe_dup_`, `uref_`, `ubuf_`, ...). Macros and enum values are
  `UPPER_CASE`. Pointer `*` sticks to the name: `struct uref *uref`.
- Variables may be declared where they are first used.
- Use the `upipe_helper_*` macros for pipe boilerplate (refcount, output, flow
  def, upump, ...) instead of reimplementing it, `UBASE_RETURN()` /
  `UBASE_ALLOC_RETURN()` for error propagation, and `likely()` / `unlikely()`
  on hot paths and error checks.
- Functions return `int` error codes (`UBASE_ERR_*`); control commands use
  `va_list` and return `UBASE_ERR_UNHANDLED` for unknown commands.
- Document every function, structure and member with mkdoc comments (`/** @This
  ... */`, `@param`, `@return`); mark private items `@internal`.
- Keep inline comments terse: explain non-obvious constraints rather than
  paraphrase the code, and describe the current state, not how it got there.
- Each file starts with a copyright header, author list and
  `SPDX-License-Identifier`. Most libraries are MIT; `upipe-ts`,
  `upipe-framers`, `upipe-filters` and a few others are LGPL-2.1-or-later. Use
  the license of the library you contribute to.
- Public headers use `_UPIPE_<DIR>_<NAME>_H_` include guards and `extern "C"`
  blocks for C++.

The semantic rules pipes must follow (threading, probe events, buffer
ownership) are described in [`doc/rules.mkdoc`](doc/rules.mkdoc).
