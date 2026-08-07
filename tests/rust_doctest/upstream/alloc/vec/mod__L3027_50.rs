// Extracted from library/alloc/src/vec/mod.rs:3027
#![allow(unused)]
extern crate alloc;
fn main() {
    // Allocate vector big enough for 10 elements.
    let mut v = Vec::with_capacity(10);

    // Fill in the first 3 elements.
    let uninit = v.spare_capacity_mut();
    uninit[0].write(0);
    uninit[1].write(1);
    uninit[2].write(2);

    // Mark the first 3 elements of the vector as being initialized.
    unsafe {
        v.set_len(3);
    }

    assert_eq!(&v, &[0, 1, 2]);
}
