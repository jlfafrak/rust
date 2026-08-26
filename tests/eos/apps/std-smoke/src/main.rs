#![feature(restricted_std)]

use std::collections::HashMap;
use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::process::Command;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Instant, SystemTime};

fn main() -> std::io::Result<()> {
    let arguments: Vec<_> = env::args_os().collect();
    let environment: HashMap<_, _> = env::vars_os().collect();

    let mut file = File::options()
        .create(true)
        .truncate(true)
        .read(true)
        .write(true)
        .open("eos-std-smoke.txt")?;
    file.write_all(b"eos")?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;

    let shared = Arc::new(Mutex::new((arguments.len(), environment.len())));
    let worker_state = Arc::clone(&shared);
    thread::spawn(move || {
        let mut counts = worker_state.lock().unwrap();
        counts.0 += 1;
    })
    .join()
    .unwrap();

    let _monotonic = Instant::now();
    let _realtime = SystemTime::now();

    // Type-check the networking and process builders without requiring a
    // live EOS endpoint or child application in this no-run smoke program.
    let _network = TcpStream::connect("127.0.0.1:9");
    let mut command = Command::new("/eos/std-smoke-child");
    command.arg("--type-check").env("EOS_STD_SMOKE", "1");
    let _child = command.spawn();

    Ok(())
}
