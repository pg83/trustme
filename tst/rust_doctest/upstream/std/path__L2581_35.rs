// Extracted from library/std/src/path.rs:2581
#![allow(unused)]
fn main() {
    use std::path::Path;

    let path = Path::new("/etc/passwd");

    assert!(path.starts_with("/etc"));
    assert!(path.starts_with("/etc/"));
    assert!(path.starts_with("/etc/passwd"));
    assert!(path.starts_with("/etc/passwd/")); // extra slash is okay
    assert!(path.starts_with("/etc/passwd///")); // multiple extra slashes are okay

    assert!(!path.starts_with("/e"));
    assert!(!path.starts_with("/etc/passwd.txt"));

    assert!(!Path::new("/etc/foo.rs").starts_with("/etc/foo"));
}
