// Extracted from library/std/src/path.rs:2848
#![allow(unused)]
#![feature(path_add_extension)]
fn main() {
    
    use std::path::{Path, PathBuf};
    
    let path = Path::new("foo.rs");
    assert_eq!(path.with_added_extension("txt"), PathBuf::from("foo.rs.txt"));
    
    let path = Path::new("foo.tar.gz");
    assert_eq!(path.with_added_extension(""), PathBuf::from("foo.tar.gz"));
    assert_eq!(path.with_added_extension("xz"), PathBuf::from("foo.tar.gz.xz"));
    assert_eq!(path.with_added_extension("").with_added_extension("txt"), PathBuf::from("foo.tar.gz.txt"));
}
