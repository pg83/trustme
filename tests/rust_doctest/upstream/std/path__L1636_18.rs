// Extracted from library/std/src/path.rs:1636
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let mut path = PathBuf::from("/foo");
    
    path.push("bar");
    assert_eq!(path, Path::new("/foo/bar"));
    
    // OsString's `push` does not add a separator.
    path.as_mut_os_string().push("baz");
    assert_eq!(path, Path::new("/foo/barbaz"));
}
