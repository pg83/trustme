// Extracted from library/core/src/intrinsics/mod.rs:770
#![allow(unused)]
fn main() {
    use std::{slice, mem};
    
    // There are multiple ways to do this, and there are multiple problems
    // with the following (transmute) way.
    fn split_at_mut_transmute<T>(slice: &mut [T], mid: usize)
                                 -> (&mut [T], &mut [T]) {
        let len = slice.len();
        assert!(mid <= len);
        unsafe {
            let slice2 = mem::transmute::<&mut [T], &mut [T]>(slice);
            // first: transmute is not type safe; all it checks is that T and
            // U are of the same size. Second, right here, you have two
            // mutable references pointing to the same memory.
            (&mut slice[0..mid], &mut slice2[mid..len])
        }
    }
    
    // This gets rid of the type safety problems; `&mut *` will *only* give
    // you a `&mut T` from a `&mut T` or `*mut T`.
    fn split_at_mut_casts<T>(slice: &mut [T], mid: usize)
                             -> (&mut [T], &mut [T]) {
        let len = slice.len();
        assert!(mid <= len);
        unsafe {
            let slice2 = &mut *(slice as *mut [T]);
            // however, you still have two mutable references pointing to
            // the same memory.
            (&mut slice[0..mid], &mut slice2[mid..len])
        }
    }
    
    // This is how the standard library does it. This is the best method, if
    // you need to do something like this
    fn split_at_stdlib<T>(slice: &mut [T], mid: usize)
                          -> (&mut [T], &mut [T]) {
        let len = slice.len();
        assert!(mid <= len);
        unsafe {
            let ptr = slice.as_mut_ptr();
            // This now has three mutable references pointing at the same
            // memory. `slice`, the rvalue ret.0, and the rvalue ret.1.
            // `slice` is never used after `let ptr = ...`, and so one can
            // treat it as "dead", and therefore, you only have two real
            // mutable slices.
            (slice::from_raw_parts_mut(ptr, mid),
             slice::from_raw_parts_mut(ptr.add(mid), len - mid))
        }
    }
}
