// Extracted from library/core/src/hint.rs:150
#![allow(unused)]
fn main() {
    use core::hint;

    /// # Safety
    ///
    /// `p` must be nonnull and valid
    pub unsafe fn next_value(p: *const i32) -> i32 {
        // SAFETY: caller invariants guarantee that `p` is not null
        unsafe { hint::assert_unchecked(!p.is_null()) }

        if p.is_null() {
            return -1;
        } else {
            // SAFETY: caller invariants guarantee that `p` is valid
            unsafe { *p + 1 }
        }
    }
}
