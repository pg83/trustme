// Extracted from library/std/src/path.rs:2250
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let os_str = Path::new("foo.txt").as_os_str();
    assert_eq!(os_str, std::ffi::OsStr::new("foo.txt"));
}
