// Extracted from library/std/src/keyword_docs.rs:2025
#![allow(unused)]
#![allow(dead_code)]
#![deny(unsafe_op_in_unsafe_fn)]
fn main() {

    /// Dereference the given pointer.
    ///
    /// # Safety
    ///
    /// `ptr` must be aligned and must not be dangling.
    unsafe fn deref_unchecked(ptr: *const i32) -> i32 {
        // SAFETY: the caller is required to ensure that `ptr` is aligned and dereferenceable.
        unsafe { *ptr }
    }

    let a = 3;
    let b = &a as *const _;
    // SAFETY: `a` has not been dropped and references are always aligned,
    // so `b` is a valid address.
    unsafe { assert_eq!(*b, deref_unchecked(b)); };
}
