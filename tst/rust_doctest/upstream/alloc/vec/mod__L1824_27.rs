// Extracted from library/alloc/src/vec/mod.rs:1824
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {

    // Allocate vector big enough for 4 elements.
    let size = 4;
    let mut x: Vec<i32> = Vec::with_capacity(size);
    let x_ptr = x.as_non_null();

    // Initialize elements via raw pointer writes, then set length.
    unsafe {
        for i in 0..size {
            x_ptr.add(i).write(i as i32);
        }
        x.set_len(size);
    }
    assert_eq!(&*x, &[0, 1, 2, 3]);
}
