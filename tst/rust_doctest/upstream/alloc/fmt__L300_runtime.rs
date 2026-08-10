// Extracted from library/alloc/src/fmt.rs:300
#![allow(unused)]
extern crate alloc;
fn main() {
    print!("{0:.1$e}", 12345, 3);
    print!("{0:.1$e}", 12355, 3);
}
