// Extracted from library/std/src/path.rs:2734
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    assert_eq!(Path::new("/etc").join("passwd"), PathBuf::from("/etc/passwd"));
    assert_eq!(Path::new("/etc").join("/bin/sh"), PathBuf::from("/bin/sh"));
}
