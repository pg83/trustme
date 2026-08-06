// Extracted from library/std/src/path.rs:2714
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    assert_eq!("rs", Path::new("foo.rs").extension().unwrap());
    assert_eq!("gz", Path::new("foo.tar.gz").extension().unwrap());
}
