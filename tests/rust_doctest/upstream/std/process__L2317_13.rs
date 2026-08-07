// Extracted from library/std/src/process.rs:2317
#![allow(unused)]
fn main() {
    use std::process::{Command, Stdio};

    let child = Command::new("/bin/cat")
        .arg("file.txt")
        .stdout(Stdio::piped())
        .spawn()
        .expect("failed to execute child");

    let output = child
        .wait_with_output()
        .expect("failed to wait on child");

    assert!(output.status.success());
}
