// Extracted from library/std/src/env.rs:71
#![allow(unused)]
fn main() {
    use std::env;
    use std::path::Path;

    let root = Path::new("/");
    assert!(env::set_current_dir(&root).is_ok());
    println!("Successfully changed working directory to {}!", root.display());
}
