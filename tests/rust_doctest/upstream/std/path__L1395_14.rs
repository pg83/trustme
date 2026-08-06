// Extracted from library/std/src/path.rs:1395
#![allow(unused)]
fn main() {
    use std::path::{Path, PathBuf};
    
    let mut p = PathBuf::from("/spirited/away.rs");
    
    p.pop();
    assert_eq!(Path::new("/spirited"), p);
    p.pop();
    assert_eq!(Path::new("/"), p);
}
