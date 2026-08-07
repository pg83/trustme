// Extracted from library/std/src/path.rs:2796
#![allow(unused)]
fn main() {
    use std::path::Path;

    let path = Path::new("foo.tar.gz");
    assert_eq!(path.with_extension("xz"), Path::new("foo.tar.xz"));
    assert_eq!(path.with_extension("").with_extension("txt"), Path::new("foo.txt"));
}
