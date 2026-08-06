// Extracted from library/std/src/macros.rs:35
#![allow(unused)]
fn main() {
    use std::io::{stdout, Write};
    
    let mut lock = stdout().lock();
    write!(lock, "hello world").unwrap();
}
