// Extracted from library/alloc/src/vec/mod.rs:234
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec![0, 2, 4, 6];
    println!("{}", v[6]); // it will panic!
}
