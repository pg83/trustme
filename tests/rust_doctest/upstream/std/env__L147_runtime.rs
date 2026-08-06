// Extracted from library/std/src/env.rs:147
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // Print all environment variables.
        for (key, value) in std::env::vars_os() {
            println!("{key:?}: {value:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
