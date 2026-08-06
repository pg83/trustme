// Extracted from library/std/src/path.rs:1659
#![allow(unused)]
fn main() {
    use std::path::PathBuf;
    
    let p = PathBuf::from("/the/head");
    let os_str = p.into_os_string();
}
