// Extracted from library/std/src/env.rs:120
#![allow(unused)]
fn main() {
    // Print all environment variables.
    for (key, value) in std::env::vars() {
        println!("{key}: {value}");
    }
}
