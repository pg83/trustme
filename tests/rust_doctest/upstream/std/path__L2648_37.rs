// Extracted from library/std/src/path.rs:2648
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    assert_eq!("foo", Path::new("foo.rs").file_stem().unwrap());
    assert_eq!("foo.tar", Path::new("foo.tar.gz").file_stem().unwrap());
}
