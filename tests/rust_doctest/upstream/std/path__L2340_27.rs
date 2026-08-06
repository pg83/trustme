// Extracted from library/std/src/path.rs:2340
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let path_buf = Path::new("foo.txt").to_path_buf();
    assert_eq!(path_buf, PathBuf::from("foo.txt"));
}
