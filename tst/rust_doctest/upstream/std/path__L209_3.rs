// Extracted from library/std/src/path.rs:209
#![allow(unused)]
fn main() {
    use std::path::Prefix::*;
    use std::ffi::OsStr;

    assert!(Verbatim(OsStr::new("pictures")).is_verbatim());
    assert!(VerbatimUNC(OsStr::new("server"), OsStr::new("share")).is_verbatim());
    assert!(VerbatimDisk(b'C').is_verbatim());
    assert!(!DeviceNS(OsStr::new("BrainInterface")).is_verbatim());
    assert!(!UNC(OsStr::new("server"), OsStr::new("share")).is_verbatim());
    assert!(!Disk(b'C').is_verbatim());
}
