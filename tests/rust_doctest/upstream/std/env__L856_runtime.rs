// Extracted from library/std/src/env.rs:856
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::env;
        
        // Prints each argument on a separate line
        for argument in env::args_os() {
            println!("{argument:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
