// Extracted from library/std/src/path.rs:1501
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let mut p = PathBuf::from("/feel/the");
    
    p.set_extension("force");
    assert_eq!(Path::new("/feel/the.force"), p.as_path());
    
    p.set_extension("dark.side");
    assert_eq!(Path::new("/feel/the.dark.side"), p.as_path());
    
    p.set_extension("cookie");
    assert_eq!(Path::new("/feel/the.dark.cookie"), p.as_path());
    
    p.set_extension("");
    assert_eq!(Path::new("/feel/the.dark"), p.as_path());
    
    p.set_extension("");
    assert_eq!(Path::new("/feel/the"), p.as_path());
    
    p.set_extension("");
    assert_eq!(Path::new("/feel/the"), p.as_path());
}
