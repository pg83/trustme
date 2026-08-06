// Extracted from library/std/src/env.rs:213
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::env;
        
        let key = "HOME";
        match env::var(key) {
            Ok(val) => println!("{key}: {val:?}"),
            Err(e) => println!("couldn't interpret {key}: {e}"),
        }
        Ok(())
    }
    doctest().unwrap();
}
