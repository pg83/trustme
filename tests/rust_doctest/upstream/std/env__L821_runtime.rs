// Extracted from library/std/src/env.rs:821
#![allow(unused)]
fn main() {
    use std::env;

    // Prints each argument on a separate line
    for argument in env::args() {
        println!("{argument}");
    }
}
