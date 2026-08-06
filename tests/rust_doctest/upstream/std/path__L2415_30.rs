// Extracted from library/std/src/path.rs:2415
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    assert!(Path::new("/etc/passwd").has_root());
}
