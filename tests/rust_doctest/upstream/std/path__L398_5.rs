// Extracted from library/std/src/path.rs:398
#![allow(unused)]
fn main() {
    if cfg!(windows) {
    use std::path::{Component, Path, Prefix};
    use std::ffi::OsStr;

    let path = Path::new(r"c:\you\later\");
    match path.components().next().unwrap() {
        Component::Prefix(prefix_component) => {
            assert_eq!(Prefix::Disk(b'C'), prefix_component.kind());
            assert_eq!(OsStr::new("c:"), prefix_component.as_os_str());
        }
        _ => unreachable!(),
    }
    }
}
