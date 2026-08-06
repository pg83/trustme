// Extracted from library/std/src/path.rs:1901
#![allow(unused)]
fn main() {
    use std::path::PathBuf;
    let path = PathBuf::from_iter(["/tmp", "foo", "bar"]);
    assert_eq!(path, PathBuf::from("/tmp/foo/bar"));
}
