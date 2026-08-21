#![feature(restricted_std)]

use std::io::{Read, Write};
use std::process::{Command, Stdio};

fn main() -> std::io::Result<()> {
    let mut child = Command::new("/eos/rust-process-child")
        .arg("--probe")
        .args(["--abi", "1.0"])
        .env("EOS_RUST_PROCESS_PROBE", "1")
        .current_dir("/eos")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()?;

    if let Some(mut input) = child.stdin.take() {
        input.write_all(b"EOS")?;
    }
    if let Some(output) = child.stdout.as_mut() {
        let mut bytes = [0_u8; 3];
        let _ = output.read(&mut bytes)?;
    }
    if child.try_wait()?.is_none() {
        child.kill()?;
    }
    let status = child.wait()?;
    assert_eq!(child.try_wait()?, Some(status));
    Ok(())
}
