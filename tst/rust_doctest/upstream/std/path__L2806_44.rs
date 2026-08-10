// Extracted from library/std/src/path.rs:2806
#![allow(unused)]
fn main() {
    use std::path::Path;

    let path = Path::new("foo");
    assert_eq!(path.with_extension("rs"), Path::new("foo.rs"));
}
