// Extracted from library/std/src/path.rs:2681
#![allow(unused)]
#![feature(path_file_prefix)]
fn main() {
    use std::path::Path;
    
    assert_eq!("foo", Path::new("foo.rs").file_prefix().unwrap());
    assert_eq!("foo", Path::new("foo.tar.gz").file_prefix().unwrap());
}
