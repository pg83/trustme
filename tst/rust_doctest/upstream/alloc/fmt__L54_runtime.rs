// Extracted from library/alloc/src/fmt.rs:54
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    format!("{1} {} {0} {}", 1, 2); // => "2 1 1 2"
}
