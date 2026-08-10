// Extracted from library/std/src/path.rs:684
#![allow(unused)]
fn main() {
    use std::path::Path;

    let mut components = Path::new("/tmp/foo/bar.txt").components();
    components.next();
    components.next();

    assert_eq!(Path::new("foo/bar.txt"), components.as_path());
}
