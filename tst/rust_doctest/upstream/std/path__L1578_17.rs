// Extracted from library/std/src/path.rs:1578
#![allow(unused)]
#![feature(path_add_extension)]
fn main() {

    use std::path::{Path, PathBuf};

    let mut p = PathBuf::from("/feel/the");

    p.add_extension("formatted");
    assert_eq!(Path::new("/feel/the.formatted"), p.as_path());

    p.add_extension("dark.side");
    assert_eq!(Path::new("/feel/the.formatted.dark.side"), p.as_path());

    p.set_extension("cookie");
    assert_eq!(Path::new("/feel/the.formatted.dark.cookie"), p.as_path());

    p.set_extension("");
    assert_eq!(Path::new("/feel/the.formatted.dark"), p.as_path());

    p.add_extension("");
    assert_eq!(Path::new("/feel/the.formatted.dark"), p.as_path());
}
