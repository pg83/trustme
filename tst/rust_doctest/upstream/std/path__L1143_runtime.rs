// Extracted from library/std/src/path.rs:1143
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let path: PathBuf = [r"C:\", "windows", "system32.dll"].iter().collect();
}
