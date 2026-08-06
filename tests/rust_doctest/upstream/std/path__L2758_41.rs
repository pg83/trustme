// Extracted from library/std/src/path.rs:2758
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let path = Path::new("/tmp/foo.png");
    assert_eq!(path.with_file_name("bar"), PathBuf::from("/tmp/bar"));
    assert_eq!(path.with_file_name("bar.txt"), PathBuf::from("/tmp/bar.txt"));
    
    let path = Path::new("/tmp");
    assert_eq!(path.with_file_name("var"), PathBuf::from("/var"));
}
