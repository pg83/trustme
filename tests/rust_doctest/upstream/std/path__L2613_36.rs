// Extracted from library/std/src/path.rs:2613
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("/etc/resolv.conf");
    
    assert!(path.ends_with("resolv.conf"));
    assert!(path.ends_with("etc/resolv.conf"));
    assert!(path.ends_with("/etc/resolv.conf"));
    
    assert!(!path.ends_with("/resolv.conf"));
    assert!(!path.ends_with("conf")); // use .extension() instead
}
