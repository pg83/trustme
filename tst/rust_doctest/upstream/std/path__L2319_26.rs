// Extracted from library/std/src/path.rs:2319
#![allow(unused)]
fn main() {
    use std::path::Path;

    let path = Path::new("foo.txt");
    assert_eq!(path.to_string_lossy(), "foo.txt");
}
