// Extracted from library/std/src/path.rs:1923
#![allow(unused)]
fn main() {
    use std::path::PathBuf;
    let mut path = PathBuf::from("/tmp");
    path.extend(["foo", "bar", "file.txt"]);
    assert_eq!(path, PathBuf::from("/tmp/foo/bar/file.txt"));
}
