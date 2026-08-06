// Extracted from library/alloc/src/vec/mod.rs:3084
#![allow(unused)]
#![feature(vec_split_at_spare)]
extern crate alloc;
fn main() {
    
    let mut v = vec![1, 1, 2];
    
    // Reserve additional space big enough for 10 elements.
    v.reserve(10);
    
    let (init, uninit) = v.split_at_spare_mut();
    let sum = init.iter().copied().sum::<u32>();
    
    // Fill in the next 4 elements.
    uninit[0].write(sum);
    uninit[1].write(sum * 2);
    uninit[2].write(sum * 3);
    uninit[3].write(sum * 4);
    
    // Mark the 4 elements of the vector as being initialized.
    unsafe {
        let len = v.len();
        v.set_len(len + 4);
    }
    
    assert_eq!(&v, &[1, 1, 2, 4, 8, 12, 16]);
}
