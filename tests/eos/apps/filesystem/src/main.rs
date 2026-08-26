#![feature(restricted_std)]

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;

fn main() -> std::io::Result<()> {
    let root = PathBuf::from("eos-filesystem-probe");
    fs::create_dir_all(&root)?;
    let original = root.join("payload.bin");
    let renamed = root.join("renamed.bin");

    let mut file = OpenOptions::new()
        .create(true)
        .truncate(true)
        .read(true)
        .write(true)
        .open(&original)?;
    file.write_all(b"EOS Rust filesystem")?;
    file.seek(SeekFrom::Start(4))?;
    let mut payload = Vec::new();
    file.read_to_end(&mut payload)?;
    assert_eq!(payload, b"Rust filesystem");
    assert!(file.metadata()?.is_file());
    drop(file);

    fs::rename(&original, &renamed)?;
    let entries: Vec<_> = fs::read_dir(&root)?.collect::<Result<_, _>>()?;
    assert_eq!(entries.len(), 1);
    let mut reopened = File::open(&renamed)?;
    payload.clear();
    reopened.read_to_end(&mut payload)?;
    assert_eq!(payload, b"EOS Rust filesystem");
    fs::remove_file(renamed)?;
    fs::remove_dir(root)?;
    Ok(())
}
