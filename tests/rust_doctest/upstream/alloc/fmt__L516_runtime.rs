// Extracted from library/alloc/src/fmt.rs:516
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    use std::io::Write;
    let mut w = Vec::new();
    write!(&mut w, "Hello {}!", "world");
}
