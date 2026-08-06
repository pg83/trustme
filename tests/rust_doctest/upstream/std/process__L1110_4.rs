// Extracted from library/std/src/process.rs:1110
#![allow(unused)]
fn main() {
    use std::process::Command;
    
    let cmd = Command::new("echo");
    assert_eq!(cmd.get_program(), "echo");
}
