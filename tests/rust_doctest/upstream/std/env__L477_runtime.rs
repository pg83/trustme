// Extracted from library/std/src/env.rs:477
#![allow(unused)]
fn main() {
    use std::env;
    
    let key = "PATH";
    match env::var_os(key) {
        Some(paths) => {
            for path in env::split_paths(&paths) {
                println!("'{}'", path.display());
            }
        }
        None => println!("{key} is not defined in the environment.")
    }
}
