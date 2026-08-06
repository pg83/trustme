// Extracted from library/std/src/fs.rs:3180
#![allow(unused)]
fn main() {
    use std::fs::DirBuilder;
    
    let mut builder = DirBuilder::new();
    builder.recursive(true);
}
