// Extracted from library/std/src/macros.rs:100
#![allow(unused)]
fn main() {
    use std::io::{stdout, Write};
    
    let mut lock = stdout().lock();
    writeln!(lock, "hello world").unwrap();
}
