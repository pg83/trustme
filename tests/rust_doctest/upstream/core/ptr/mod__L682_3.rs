// Extracted from library/core/src/ptr/mod.rs:682
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let mut vec = vec![0u32; 4];
    unsafe {
        let vec_ptr = vec.as_mut_ptr();
        ptr::write_bytes(vec_ptr, 0xfe, 2);
    }
    assert_eq!(vec, [0xfefefefe, 0xfefefefe, 0, 0]);
}
