// Extracted from library/core/src/ptr/non_null.rs:377
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let mut x = 0u32;
    let ptr = NonNull::new(&mut x).expect("ptr is null!");
    
    let x_value = unsafe { *ptr.as_ptr() };
    assert_eq!(x_value, 0);
    
    unsafe { *ptr.as_ptr() += 2; }
    let x_value = unsafe { *ptr.as_ptr() };
    assert_eq!(x_value, 2);
}
