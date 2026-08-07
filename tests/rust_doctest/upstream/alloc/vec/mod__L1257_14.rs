// Extracted from library/alloc/src/vec/mod.rs:1257
#![allow(unused)]
#![feature(allocator_api, vec_into_raw_parts, box_vec_non_null)]
extern crate alloc;
fn main() {

    use std::alloc::System;

    let mut v: Vec<i32, System> = Vec::new_in(System);
    v.push(-1);
    v.push(0);
    v.push(1);

    let (ptr, len, cap, alloc) = v.into_parts_with_alloc();

    let rebuilt = unsafe {
        // We can now make changes to the components, such as
        // transmuting the raw pointer to a compatible type.
        let ptr = ptr.cast::<u32>();

        Vec::from_parts_in(ptr, len, cap, alloc)
    };
    assert_eq!(rebuilt, [4294967295, 0, 1]);
}
