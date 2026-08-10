// Extracted from library/std/src/path.rs:110
#![allow(unused)]
fn main() {
    use std::path::{Component, Path, Prefix};
    use std::path::Prefix::*;
    use std::ffi::OsStr;

    fn get_path_prefix(s: &str) -> Prefix<'_> {
        let path = Path::new(s);
        match path.components().next().unwrap() {
            Component::Prefix(prefix_component) => prefix_component.kind(),
            _ => panic!(),
        }
    }

    if cfg!(windows) {
    assert_eq!(Verbatim(OsStr::new("pictures")),
               get_path_prefix(r"\\?\pictures\kittens"));
    assert_eq!(VerbatimUNC(OsStr::new("server"), OsStr::new("share")),
               get_path_prefix(r"\\?\UNC\server\share"));
    assert_eq!(VerbatimDisk(b'C'), get_path_prefix(r"\\?\c:\"));
    assert_eq!(DeviceNS(OsStr::new("BrainInterface")),
               get_path_prefix(r"\\.\BrainInterface"));
    assert_eq!(UNC(OsStr::new("server"), OsStr::new("share")),
               get_path_prefix(r"\\server\share"));
    assert_eq!(Disk(b'C'), get_path_prefix(r"C:\Users\Rust\Pictures\Ferris"));
    }
}
