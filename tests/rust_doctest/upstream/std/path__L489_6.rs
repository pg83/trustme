// Extracted from library/std/src/path.rs:489
#![allow(unused)]
fn main() {
    use std::path::{Component, Path};

    let path = Path::new("/tmp/foo/bar.txt");
    let components = path.components().collect::<Vec<_>>();
    assert_eq!(&components, &[
        Component::RootDir,
        Component::Normal("tmp".as_ref()),
        Component::Normal("foo".as_ref()),
        Component::Normal("bar.txt".as_ref()),
    ]);
}
