// Extracted from library/core/src/primitive_docs.rs:1699
#![allow(unused)]
fn main() {
    fn add_one(x: usize) -> usize {
        x + 1
    }
    
    unsafe fn add_one_unsafely(x: usize) -> usize {
        x + 1
    }
    
    let safe_ptr: fn(usize) -> usize = add_one;
    
    //ERROR: mismatched types: expected normal fn, found unsafe fn
    //let bad_ptr: fn(usize) -> usize = add_one_unsafely;
    
    let unsafe_ptr: unsafe fn(usize) -> usize = add_one_unsafely;
    let really_safe_ptr: unsafe fn(usize) -> usize = add_one;
}
