// Extracted from library/std/src/path.rs:1292
#![allow(unused)]
fn main() {
    use std::path::PathBuf;
    
    let mut path = PathBuf::from("/tmp");
    path.push("/etc");
    assert_eq!(path, PathBuf::from("/etc"));
}
