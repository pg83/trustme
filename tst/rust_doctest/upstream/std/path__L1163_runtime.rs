// Extracted from library/std/src/path.rs:1163
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let mut path = PathBuf::new();

    path.push(r"C:\");
    path.push("windows");
    path.push(r"..\otherdir");
    path.push("system32");
}
