// Extracted from library/std/src/path.rs:1229
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let p = PathBuf::from("/test");
    assert_eq!(Path::new("/test"), p.as_path());
}
