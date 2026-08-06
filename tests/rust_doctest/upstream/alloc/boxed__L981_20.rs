// Extracted from library/alloc/src/boxed.rs:981
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut values = Box::<[u32]>::new_uninit_slice(3);
    // Deferred initialization:
    values[0].write(1);
    values[1].write(2);
    values[2].write(3);
    let values = unsafe { values.assume_init() };
    
    assert_eq!(*values, [1, 2, 3])
}
