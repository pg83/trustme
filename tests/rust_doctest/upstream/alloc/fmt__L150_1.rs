// Extracted from library/alloc/src/fmt.rs:150
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(format!("Hello {:<5}!", "x"),  "Hello x    !");
    assert_eq!(format!("Hello {:-<5}!", "x"), "Hello x----!");
    assert_eq!(format!("Hello {:^5}!", "x"),  "Hello   x  !");
    assert_eq!(format!("Hello {:>5}!", "x"),  "Hello     x!");
}
