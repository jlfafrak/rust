#![feature(restricted_std)]

use std::io;
use std::os::unix::process::CommandExt;
use std::process::{Child, Command, ExitStatus, Stdio};

fn spawn_with_ephemeral_request() -> io::Result<Child> {
    let program = String::from("/eos/process-smoke-child");
    let arguments = [String::from("--mode"), String::from("process-smoke")];
    let environment = (String::from("EOS_PROCESS_SMOKE"), String::from("1"));
    let working_directory = String::from("/eos");

    let mut command = Command::new(program);
    command
        .args(arguments)
        .env(environment.0, environment.1)
        .current_dir(working_directory)
        .stdout(Stdio::piped());
    command.spawn()
}

fn decode_status(status: ExitStatus) -> (bool, Option<i32>) {
    (status.success(), status.code())
}

fn main() -> io::Result<()> {
    let exec_error = Command::new("/eos/process-smoke-child").exec();
    assert_eq!(exec_error.kind(), io::ErrorKind::Unsupported);

    // Returning only the Child proves that Command's owned argv/env/cwd storage
    // may be dropped immediately after the synchronous spawn call returns.
    let mut child = spawn_with_ephemeral_request()?;
    assert!(child.stdout.is_some());

    if child.try_wait()?.is_none() {
        child.kill()?;
    }

    let status = child.wait()?;
    let repeated = child.wait()?;
    assert_eq!(status, repeated);
    assert_eq!(child.try_wait()?, Some(status));
    child.kill()?;
    child.kill()?;
    let (_success, code) = decode_status(status);
    let _normal_exit_code = code;

    Ok(())
}
