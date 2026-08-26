#![feature(restricted_std)]

use std::env;
use std::io::{Read, Write};
use std::process::{Command, Stdio};

fn main() -> std::io::Result<()> {
    if env::args().any(|argument| argument == "--child") {
        let mut bytes = [0_u8; 3];
        std::io::stdin().read_exact(&mut bytes)?;
        std::io::stdout().write_all(&bytes)?;
        return Ok(());
    }

    let arguments = env::args_os().count();
    let environment = env::vars_os().count();
    let current_directory = env::current_dir()?;
    let executable = env::current_exe()?;
    println!(
        "process parent: arguments={arguments} environment={environment} cwd={}",
        current_directory.display()
    );

    let mut child = Command::new(executable)
        .arg("--child")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()?;

    child
        .stdin
        .take()
        .expect("piped child stdin must be available")
        .write_all(b"EOS")?;
    let mut echoed = [0_u8; 3];
    child
        .stdout
        .as_mut()
        .expect("piped child stdout must be available")
        .read_exact(&mut echoed)?;
    assert_eq!(&echoed, b"EOS", "child must echo the fixed probe bytes");

    let first_status = child.try_wait()?;
    let killed = first_status.is_none();
    if killed {
        child.kill()?;
    }
    let status = child.wait()?;
    let repeated_status = child.wait()?;
    assert_eq!(repeated_status, status, "repeated wait must be stable");
    println!(
        "process child: echo=EOS try_wait_ready={} killed={killed} repeated_wait=true",
        first_status.is_some()
    );
    Ok(())
}
