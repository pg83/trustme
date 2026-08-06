// Extracted from library/core/src/result.rs:948
#![allow(unused)]
fn main() {
    use std::{fs, io};
    
    fn read() -> io::Result<String> {
        fs::read_to_string("address.txt")
            .inspect_err(|e| eprintln!("failed to read file: {e}"))
    }
}
