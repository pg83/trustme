// Extracted from library/std/src/path.rs:2543
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};

    let path = Path::new("/test/haha/foo.txt");

    assert_eq!(path.strip_prefix("/"), Ok(Path::new("test/haha/foo.txt")));
    assert_eq!(path.strip_prefix("/test"), Ok(Path::new("haha/foo.txt")));
    assert_eq!(path.strip_prefix("/test/"), Ok(Path::new("haha/foo.txt")));
    assert_eq!(path.strip_prefix("/test/haha/foo.txt"), Ok(Path::new("")));
    assert_eq!(path.strip_prefix("/test/haha/foo.txt/"), Ok(Path::new("")));

    assert!(path.strip_prefix("test").is_err());
    assert!(path.strip_prefix("/te").is_err());
    assert!(path.strip_prefix("/haha").is_err());

    let prefix = PathBuf::from("/test/");
    assert_eq!(path.strip_prefix(prefix), Ok(Path::new("haha/foo.txt")));
}
