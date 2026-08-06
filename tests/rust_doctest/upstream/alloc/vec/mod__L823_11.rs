// Extracted from library/alloc/src/vec/mod.rs:823
#![allow(unused)]
#![feature(vec_into_raw_parts, box_vec_non_null)]
extern crate alloc;
fn main() {
    
    let v: Vec<i32> = vec![-1, 0, 1];
    
    let (ptr, len, cap) = v.into_parts();
    
    let rebuilt = unsafe {
        // We can now make changes to the components, such as
        // transmuting the raw pointer to a compatible type.
        let ptr = ptr.cast::<u32>();
    
        Vec::from_parts(ptr, len, cap)
    };
    assert_eq!(rebuilt, [4294967295, 0, 1]);
}
