// Extracted from library/core/src/ptr/mod.rs:1553
#![allow(unused)]
fn main() {
    use std::ptr;

    let mut rust = vec!['b', 'u', 's', 't'];

    // `mem::replace` would have the same effect without requiring the unsafe
    // block.
    let b = unsafe {
        ptr::replace(&mut rust[0], 'r')
    };

    assert_eq!(b, 'b');
    assert_eq!(rust, &['r', 'u', 's', 't']);
}
