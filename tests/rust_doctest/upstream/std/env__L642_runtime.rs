// Extracted from library/std/src/env.rs:642
#![allow(unused)]
fn main() {
    use std::env;
    
    match env::home_dir() {
        Some(path) => println!("Your home directory, probably: {}", path.display()),
        None => println!("Impossible to get your home dir!"),
    }
}
