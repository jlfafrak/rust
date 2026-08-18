#![feature(restricted_std)]

use std::cell::Cell;
use std::panic::{AssertUnwindSafe, catch_unwind};

struct DropGuard<'a>(&'a Cell<bool>);

struct CleanupPanic;

impl Drop for DropGuard<'_> {
    fn drop(&mut self) {
        self.0.set(true);
    }
}

impl Drop for CleanupPanic {
    fn drop(&mut self) {
        panic!("EOS Rust TLS cleanup panic probe");
    }
}

std::thread_local! {
    static CLEANUP_PANIC: CleanupPanic = const { CleanupPanic };
}

fn main() {
    if std::env::args().any(|argument| argument == "--cleanup-panic") {
        std::thread::spawn(|| CLEANUP_PANIC.with(|_| {}))
            .join()
            .expect("TLS cleanup panic must abort before join returns");
        panic!("TLS cleanup panic did not abort");
    }

    let dropped = Cell::new(false);
    let result = catch_unwind(AssertUnwindSafe(|| {
        let _guard = DropGuard(&dropped);
        panic!("EOS Rust unwind probe");
    }));

    assert!(result.is_err(), "catch_unwind must contain the Rust panic");
    assert!(dropped.get(), "unwinding must run Rust drop glue");
}
