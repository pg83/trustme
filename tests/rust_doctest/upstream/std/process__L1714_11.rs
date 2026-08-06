// Extracted from library/std/src/process.rs:1714
#![allow(unused)]
#![feature(exit_status_error)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        use std::process::Command;
        
        fn test() -> Result<(), Box<dyn std::error::Error>> {
        let output = Command::new("whoami")
            .stdout(io::stderr())
            .output()?;
        output.status.exit_ok()?;
        assert!(output.stdout.is_empty());
        Ok(())
        }
        
        if cfg!(all(unix, not(target_os = "android"))) {
            test().unwrap();
        }
        Ok(())
    }
    doctest().unwrap();
}
