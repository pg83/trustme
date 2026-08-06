// Extracted from library/core/src/slice/raw.rs:90
#![allow(unused)]
fn main() {
    use std::slice;
    
    /// Sum the elements of an FFI slice.
    ///
    /// # Safety
    ///
    /// If ptr is not NULL, it must be correctly aligned and
    /// point to `len` initialized items of type `f32`.
    unsafe extern "C" fn sum_slice(ptr: *const f32, len: usize) -> f32 {
        let data = if ptr.is_null() {
            // `len` is assumed to be 0.
            &[]
        } else {
            // SAFETY: see function docstring.
            unsafe { slice::from_raw_parts(ptr, len) }
        };
        data.into_iter().sum()
    }
    
    // This could be the result of C++'s std::vector::data():
    let ptr = std::ptr::null();
    // And this could be std::vector::size():
    let len = 0;
    assert_eq!(unsafe { sum_slice(ptr, len) }, 0.0);
}
