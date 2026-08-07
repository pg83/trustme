// Extracted from library/std/src/path.rs:2267
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};

    let mut path = PathBuf::from("Foo.TXT");

    assert_ne!(path, Path::new("foo.txt"));

    path.as_mut_os_str().make_ascii_lowercase();
    assert_eq!(path, Path::new("foo.txt"));
}
