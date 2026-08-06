// Extracted from library/std/src/path.rs:2786
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("foo.rs");
    assert_eq!(path.with_extension("txt"), Path::new("foo.txt"));
    assert_eq!(path.with_extension(""), Path::new("foo"));
}
