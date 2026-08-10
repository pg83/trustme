// Extracted from library/std/src/process.rs:205
#![allow(unused)]
fn main() {
    use std::process::Command;

    let mut child = Command::new("/bin/cat")
        .arg("file.txt")
        .spawn()
        .expect("failed to execute child");

    let ecode = child.wait().expect("failed to wait on child");

    assert!(ecode.success());
}
