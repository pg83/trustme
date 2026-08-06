// Extracted from library/std/src/process.rs:1130
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    use std::process::Command;
    
    let mut cmd = Command::new("echo");
    cmd.arg("first").arg("second");
    let args: Vec<&OsStr> = cmd.get_args().collect();
    assert_eq!(args, &["first", "second"]);
}
