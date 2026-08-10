// Extracted from library/std/src/process.rs:1802
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {
    if cfg!(unix) {
    use std::process::Command;

    let status = Command::new("ls")
        .arg("/dev/nonexistent")
        .status()
        .expect("ls could not be executed");

    println!("ls: {status}");
    status.exit_ok().expect_err("/dev/nonexistent could be listed!");
    } // cfg!(unix)
}
