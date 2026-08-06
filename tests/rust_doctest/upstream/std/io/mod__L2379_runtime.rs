// Extracted from library/std/src/io/mod.rs:2379
#![allow(unused)]
#![feature(buf_read_has_data_left)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        use std::io::prelude::*;
        
        let stdin = io::stdin();
        let mut stdin = stdin.lock();
        
        while stdin.has_data_left()? {
            let mut line = String::new();
            stdin.read_line(&mut line)?;
            // work with line
            println!("{line:?}");
        }
        std::io::Result::Ok(())
        Ok(())
    }
    doctest().unwrap();
}
