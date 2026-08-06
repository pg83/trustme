// Extracted from library/alloc/src/sync.rs:1167
#![allow(unused)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    
    let mut values = Arc::<[u32]>::new_uninit_slice(3);
    
    // Deferred initialization:
    let data = Arc::get_mut(&mut values).unwrap();
    data[0].write(1);
    data[1].write(2);
    data[2].write(3);
    
    let values = unsafe { values.assume_init() };
    
    assert_eq!(*values, [1, 2, 3])
}
