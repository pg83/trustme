// Extracted from library/core/src/ptr/mod.rs:596
#![allow(unused)]
fn main() {
    use std::ptr;

    /// # Safety
    ///
    /// * `ptr` must be correctly aligned for its type and non-zero.
    /// * `ptr` must be valid for reads of `elts` contiguous elements of type `T`.
    /// * Those elements must not be used after calling this function unless `T: Copy`.
    #[allow(dead_code)]
    unsafe fn from_buf_raw<T>(ptr: *const T, elts: usize) -> Vec<T> {
        let mut dst = Vec::with_capacity(elts);

        // SAFETY: Our precondition ensures the source is aligned and valid,
        // and `Vec::with_capacity` ensures that we have usable space to write them.
        unsafe { ptr::copy(ptr, dst.as_mut_ptr(), elts); }

        // SAFETY: We created it with this much capacity earlier,
        // and the previous `copy` has initialized these elements.
        unsafe { dst.set_len(elts); }
        dst
    }
}
