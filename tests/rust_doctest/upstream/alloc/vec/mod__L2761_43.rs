// Extracted from library/alloc/src/vec/mod.rs:2761
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![1, 2, 3];
    let u: Vec<_> = v.drain(1..).collect();
    assert_eq!(v, &[1]);
    assert_eq!(u, &[2, 3]);

    // A full range clears the vector, like `clear()` does
    v.drain(..);
    assert_eq!(v, &[]);
}
