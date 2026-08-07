// Extracted from library/std/src/path.rs:1128
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let mut path = PathBuf::new();

    path.push(r"C:\");
    path.push("windows");
    path.push("system32");

    path.set_extension("dll");
}
