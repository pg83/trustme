// Extracted from library/std/src/path.rs:2366
#![allow(unused)]
fn main() {
    use std::path::Path;

    assert!(!Path::new("foo.txt").is_absolute());
}
