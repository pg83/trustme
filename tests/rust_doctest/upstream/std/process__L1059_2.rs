// Extracted from library/std/src/process.rs:1059
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::process::Command;
        use std::io::{self, Write};
        let output = Command::new("/bin/cat")
            .arg("file.txt")
            .output()?;

        println!("status: {}", output.status);
        io::stdout().write_all(&output.stdout)?;
        io::stderr().write_all(&output.stderr)?;

        assert!(output.status.success());
        io::Result::Ok(())
    }
    doctest().unwrap();
}
