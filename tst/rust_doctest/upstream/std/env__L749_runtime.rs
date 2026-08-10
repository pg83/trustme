// Extracted from library/std/src/env.rs:749
#![allow(unused)]
fn main() {
    use std::env;

    match env::current_exe() {
        Ok(exe_path) => println!("Path of this executable is: {}",
                                 exe_path.display()),
        Err(e) => println!("failed to get current exe path: {e}"),
    };
}
