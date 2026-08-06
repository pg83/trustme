// Extracted from library/std/src/path.rs:2511
#![allow(unused)]
fn main() {
    use std::path::Path;
    use std::ffi::OsStr;
    
    assert_eq!(Some(OsStr::new("bin")), Path::new("/usr/bin/").file_name());
    assert_eq!(Some(OsStr::new("foo.txt")), Path::new("tmp/foo.txt").file_name());
    assert_eq!(Some(OsStr::new("foo.txt")), Path::new("foo.txt/.").file_name());
    assert_eq!(Some(OsStr::new("foo.txt")), Path::new("foo.txt/.//").file_name());
    assert_eq!(None, Path::new("foo.txt/..").file_name());
    assert_eq!(None, Path::new("/").file_name());
}
