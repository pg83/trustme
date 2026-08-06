// Extracted from library/core/src/ptr/non_null.rs:455
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let mut x = 0u32;
    let mut ptr = NonNull::new(&mut x).expect("null pointer");
    
    let x_ref = unsafe { ptr.as_mut() };
    assert_eq!(*x_ref, 0);
    *x_ref += 2;
    assert_eq!(*x_ref, 2);
}
