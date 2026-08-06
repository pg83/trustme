// Extracted from library/core/src/macros/mod.rs:1398
#![allow(unused)]
fn main() {
    let my_directory = if cfg!(windows) {
        "windows-specific-directory"
    } else {
        "unix-directory"
    };
}
