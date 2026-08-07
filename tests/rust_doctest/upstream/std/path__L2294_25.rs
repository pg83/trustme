// Extracted from library/std/src/path.rs:2294
#![allow(unused)]
fn main() {
    use std::path::Path;

    let path = Path::new("foo.txt");
    assert_eq!(path.to_str(), Some("foo.txt"));
}
