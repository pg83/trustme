// Extracted from library/alloc/src/vec/mod.rs:783
#![allow(unused)]
#![feature(vec_into_raw_parts)]
extern crate alloc;
fn main() {
    let v: Vec<i32> = vec![-1, 0, 1];
    
    let (ptr, len, cap) = v.into_raw_parts();
    
    let rebuilt = unsafe {
        // We can now make changes to the components, such as
        // transmuting the raw pointer to a compatible type.
        let ptr = ptr as *mut u32;
    
        Vec::from_raw_parts(ptr, len, cap)
    };
    assert_eq!(rebuilt, [4294967295, 0, 1]);
}
