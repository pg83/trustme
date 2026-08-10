// Extracted from library/std/src/process.rs:1350
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {
    #[cfg(all(unix, not(target_os = "android")))] {
    use std::process::Command;
    assert!(Command::new("false").output().unwrap().exit_ok().is_err());
    }
}
