// Extracted from library/alloc/src/vec/mod.rs:1702
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = vec![1, 2, 4];
    let x_ptr = x.as_ptr();
    
    unsafe {
        for i in 0..x.len() {
            assert_eq!(*x_ptr.add(i), 1 << i);
        }
    }
}
