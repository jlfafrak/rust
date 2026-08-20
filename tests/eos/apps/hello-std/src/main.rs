#![feature(restricted_std)]

use std::collections::HashMap;

fn main() {
    let mut messages = HashMap::new();
    messages.insert("target", "armv7a-unknown-eos-eabi");
    println!("hello from {}", messages["target"]);
}
