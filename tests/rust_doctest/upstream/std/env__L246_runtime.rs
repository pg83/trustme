// Extracted from library/std/src/env.rs:246
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::env;
        
        let key = "HOME";
        match env::var_os(key) {
            Some(val) => println!("{key}: {val:?}"),
            None => println!("{key} is not defined in the environment.")
        }
        Ok(())
    }
    doctest().unwrap();
}
