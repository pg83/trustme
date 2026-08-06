// Extracted from library/std/src/path.rs:2888
#![allow(unused)]
fn main() {
    use std::path::{Path, Component};
    use std::ffi::OsStr;
    
    let mut components = Path::new("/tmp/foo.txt").components();
    
    assert_eq!(components.next(), Some(Component::RootDir));
    assert_eq!(components.next(), Some(Component::Normal(OsStr::new("tmp"))));
    assert_eq!(components.next(), Some(Component::Normal(OsStr::new("foo.txt"))));
    assert_eq!(components.next(), None)
}
