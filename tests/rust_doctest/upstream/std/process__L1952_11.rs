// Extracted from library/std/src/process.rs:1952
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {
    #[cfg(all(unix, not(target_os = "android")))] {
    use std::process::Command;

    let bad = Command::new("false").status().unwrap().exit_ok().unwrap_err();
    assert_eq!(bad.code(), Some(1));
    } // #[cfg(unix)]
}
