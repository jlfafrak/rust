# EOS Rust source baselines

The EOS Rust SDK source lock is
`src/tools/eos-sdk/manifests/toolchain.lock.toml`. It pins semantic releases
and upstream commits for Rust and `libc`, together with the ARM GNU release,
EOS SDK baseline, and the `libeos_rust_abi` version.

Package generation records SHA-256 digests for the installed ARM toolchain and
the EOS SDK archive in the generated release manifest. Those installation
digests complement the source lock: the lock identifies the intended source
baselines, while the generated release manifest identifies the exact installed
archives used to build a package.
