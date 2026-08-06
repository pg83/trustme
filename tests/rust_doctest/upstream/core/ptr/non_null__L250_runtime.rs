// Extracted from library/core/src/ptr/non_null.rs:250
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let mut x = 0u32;
    let ptr = NonNull::<u32>::new(&mut x as *mut _).expect("ptr is null!");
    
    if let Some(ptr) = NonNull::<u32>::new(std::ptr::null_mut()) {
        unreachable!();
    }
}
