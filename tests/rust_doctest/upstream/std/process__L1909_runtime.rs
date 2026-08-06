// Extracted from library/std/src/process.rs:1909
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        if cfg!(all(unix, not(target_os = "android"))) {
        use std::process::{Command, ExitStatusError};
        
        fn run(cmd: &str) -> Result<(), ExitStatusError> {
            Command::new(cmd).status().unwrap().exit_ok()?;
            Ok(())
        }
        
        run("true").unwrap();
        run("false").unwrap_err();
        } // cfg!(unix)
        Ok(())
    }
    doctest().unwrap();
}
