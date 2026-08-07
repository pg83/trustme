// Extracted from library/core/src/mem/maybe_uninit.rs:97
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    unsafe fn make_vec(out: *mut Vec<i32>) {
        // `write` does not drop the old contents, which is important.
        unsafe { out.write(vec![1, 2, 3]); }
    }

    let mut v = MaybeUninit::uninit();
    unsafe { make_vec(v.as_mut_ptr()); }
    // Now we know `v` is initialized! This also makes sure the vector gets
    // properly dropped.
    let v = unsafe { v.assume_init() };
    assert_eq!(&v, &[1, 2, 3]);
}
