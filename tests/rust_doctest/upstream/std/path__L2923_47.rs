// Extracted from library/std/src/path.rs:2923
#![allow(unused)]
fn main() {
    use std::path::{self, Path};
    use std::ffi::OsStr;
    
    let mut it = Path::new("/tmp/foo.txt").iter();
    assert_eq!(it.next(), Some(OsStr::new(&path::MAIN_SEPARATOR.to_string())));
    assert_eq!(it.next(), Some(OsStr::new("tmp")));
    assert_eq!(it.next(), Some(OsStr::new("foo.txt")));
    assert_eq!(it.next(), None)
}
