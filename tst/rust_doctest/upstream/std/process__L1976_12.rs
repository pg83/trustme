// Extracted from library/std/src/process.rs:1976
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {

    if cfg!(all(unix, not(target_os = "android"))) {
    use std::num::NonZero;
    use std::process::Command;

    let bad = Command::new("false").status().unwrap().exit_ok().unwrap_err();
    assert_eq!(bad.code_nonzero().unwrap(), NonZero::new(1).unwrap());
    } // cfg!(unix)
}
