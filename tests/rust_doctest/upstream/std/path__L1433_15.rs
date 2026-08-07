// Extracted from library/std/src/path.rs:1433
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let mut buf = PathBuf::from("/");
    assert!(buf.file_name() == None);

    buf.set_file_name("foo.txt");
    assert!(buf == PathBuf::from("/foo.txt"));
    assert!(buf.file_name().is_some());

    buf.set_file_name("bar.txt");
    assert!(buf == PathBuf::from("/bar.txt"));

    buf.set_file_name("baz");
    assert!(buf == PathBuf::from("/baz"));

    buf.set_file_name("../b/c.txt");
    assert!(buf == PathBuf::from("/../b/c.txt"));

    buf.set_file_name("baz");
    assert!(buf == PathBuf::from("/../b/baz"));
}
