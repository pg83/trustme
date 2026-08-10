// Extracted from library/std/src/path.rs:1282
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let mut path = PathBuf::from("/tmp");
    path.push("file.bk");
    assert_eq!(path, PathBuf::from("/tmp/file.bk"));
}
