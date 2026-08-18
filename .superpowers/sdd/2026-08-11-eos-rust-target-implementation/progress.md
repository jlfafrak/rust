# SDD ledger — plan: docs/superpowers/plans/2026-08-11-eos-rust-target-implementation.md

Preflight ruling: `test_libc_links.py`, `test_pal_cfg_scope.py`, and `test_ffi_unwind_policy.py` are static compliance lints, not behavioral TDD evidence. Separate compile, link, and runtime tests remain required. Approved by the user on 2026-08-11.

Setup: isolated worktree `/mnt/c/Users/jfafrak/Documents/ChatGPT/eos-rust-target/.worktrees/eos-rust-1.97.1` on `codex/eos-rust-target`.
Setup: Rust 1.97.1 import commit `ee3c9717`; merge commit `a8ac9c53`; pinned Rust commit `8bab26f4` is an ancestor; merged tree is `3a27e3db86f7b1edcab27a52f675e0b9c1e95e50`.

Task 1: complete (commits a8ac9c5..94ba437, review clean)
Task 2: fix round 1/5 (1 addressed, 0 open; commits ff77cf4..2d8fb99)
Task 2: complete (commits 94ba437..2d8fb99, review clean)
Task 3: fix round 1/5 (6 addressed, 1 new open; commits 5954f94..75f34fe)
Task 3: fix round 2/5 (1 addressed, 0 open; commits 75f34fe..e2e6751)
Task 3: complete (commits 2d8fb99..e2e6751, review clean)
Task 4: fix round 1/5 (4 addressed, 3 open; commits 0c8d41c..d6de709)
Task 4: fix round 2/5 (1 addressed, 2 open; commits d6de709..389bbd8)
Task 4: fix round 3/5 (2 addressed, 0 open; commits 389bbd8..c02420a)
Task 4: complete (commits e2e6751..c02420a, review clean)
Task 5: fix round 1/5 (4 addressed, 0 open; commits 0a668e1..b92b0f4)
Task 5: complete (commits c02420a..b92b0f4, review clean)
Task 6: fix round 1/5 (7 addressed, 3 open; commits 5dab20f..77ef1cf)
Task 6: fix round 2/5 (3 addressed, 0 open; commits 77ef1cf..2216909)
Task 6: complete (commits b92b0f4..2216909, review clean)
Task 7: fix round 1/5 (2 addressed, 0 open; commits c4ddbe8..714ad85)
Task 7: complete (commits 2216909..714ad85, review clean)
Task 8: fix round 1/5 (4 addressed, 0 open; commits 9e48c8f..7758a2e)
Task 8: complete (commits 714ad85..7758a2e, review clean)
Task 9: fix round 1/5 (5 addressed, 1 new open; commits ecda1aa..8a5d85b)
Task 9: fix round 2/5 (1 addressed, 0 open; commits 8a5d85b..adb9d285)
Task 9: complete (commits e18afb4..adb9d285, review clean)
Task 10: minor (deferred): process tests do not mutate or release caller argv/env/cwd storage after spawn to prove deep-copy lifetime
Task 10: fix round 1/5 (3 addressed, 0 open; commits 09ed298..b45ec8c)
Task 10: complete (commits 212e5a5..b45ec8c, review clean)
Task 11: plan correction (approved 2026-08-18): defer exact `./x check library/std --target armv7a-unknown-eos-eabi` gate to Task 12 because current failures are EOS PAL/generic-Unix call-site adaptations, not libc binding defects
Task 11: fix round 1/5 (1 addressed, 1 new open; commits 426fb80..7f283b8)
Task 11: fix round 2/5 (1 addressed, 0 open; commits 7f283b8..f5efc93)
Task 11: complete (commits 1d856be..f5efc93, review clean; std gate deferred to Task 12)
Task 12: ABI correction (approved 2026-08-18): change unreleased v1 `eos_rust_hash_seed` from `void` to `int32_t` so the exact Rust random policy can observe initialization failure
Task 12: plan correction: the literal Rust 1.97.1 bootstrap `--no-run` option does not exist; use the documented equivalent `--run never`
Task 12: dependency deferral: exact std build and `--run never` compile all target sources, then stop only at the missing `eos-rust-link`; final link gates remain owned by Task 15
Task 12: scope pull-forward: the EOS process routing/backend needed for Task 12 std/`Command` checking was implemented here; remaining Task 13 validation and deliverables are untouched
Task 12: fix round 1/5 (2 addressed, 1 new open; commits 2df329f..d6fbbbe)
Task 12: fix round 2/5 (1 addressed, 0 open; commits d6fbbbe..abb2d6e)
Task 12: complete (commits edec4e3..abb2d6e, review clean; linker gates deferred to Task 15)
