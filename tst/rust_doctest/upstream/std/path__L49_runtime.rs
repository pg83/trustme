// Extracted from library/std/src/path.rs:49
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    // This way works...
    let mut path = PathBuf::from("c:\\");

    path.push("windows");
    path.push("system32");

    path.set_extension("dll");

    // ... but push is best used if you don't know everything up
    // front. If you do, this way is better:
    let path: PathBuf = ["c:\\", "windows", "system32.dll"].iter().collect();
}
