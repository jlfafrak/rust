#![feature(restricted_std)]

use std::cell::Cell;
use std::sync::{Arc, Barrier, Condvar, Mutex, OnceLock};
use std::thread;

thread_local! {
    static WORKER_NUMBER: Cell<u32> = const { Cell::new(0) };
}

static INITIALIZED: OnceLock<&'static str> = OnceLock::new();

fn main() {
    assert_eq!(*INITIALIZED.get_or_init(|| "EOS"), "EOS");
    let start = Arc::new(Barrier::new(2));
    let result = Arc::new((Mutex::new(None), Condvar::new()));
    let worker_start = Arc::clone(&start);
    let worker_result = Arc::clone(&result);

    let worker = thread::spawn(move || {
        WORKER_NUMBER.with(|number| number.set(17));
        worker_start.wait();
        let (slot, ready) = &*worker_result;
        *slot.lock().unwrap() = Some(WORKER_NUMBER.with(Cell::get));
        ready.notify_one();
    });

    start.wait();
    let (slot, ready) = &*result;
    let mut value = slot.lock().unwrap();
    while value.is_none() {
        value = ready.wait(value).unwrap();
    }
    assert_eq!(*value, Some(17));
    drop(value);
    worker.join().unwrap();
    assert_eq!(WORKER_NUMBER.with(Cell::get), 0);
}
