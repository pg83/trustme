// Extracted from library/std/src/io/stdio.rs:985
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io::{self, Write};
        
        fn foo() -> io::Result<()> {
            let stderr = io::stderr();
            let mut handle = stderr.lock();
        
            handle.write_all(b"hello world")?;
        
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
