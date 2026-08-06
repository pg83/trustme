// Extracted from library/core/src/result.rs:139
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs::File;
        use std::io::prelude::*;
        use std::io;
        #[allow(dead_code)]
        fn write_message() -> io::Result<()> {
            let mut file = File::create("valuable_data.txt")?;
            file.write_all(b"important message")?;
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
