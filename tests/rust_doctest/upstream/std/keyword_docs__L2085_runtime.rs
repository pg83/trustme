// Extracted from library/std/src/keyword_docs.rs:2085
#![allow(unused)]
#![feature(never_type)]
#![deny(unsafe_op_in_unsafe_fn)]
fn main() {
    
    trait Indexable {
        const LEN: usize;
    
        /// # Safety
        ///
        /// The caller must ensure that `idx < LEN`.
        unsafe fn idx_unchecked(&self, idx: usize) -> i32;
    }
    
    // The implementation for `i32` doesn't need to do any contract reasoning.
    impl Indexable for i32 {
        const LEN: usize = 1;
    
        /// See `Indexable` for the safety contract.
        unsafe fn idx_unchecked(&self, idx: usize) -> i32 {
            debug_assert_eq!(idx, 0);
            *self
        }
    }
    
    // The implementation for arrays exploits the function contract to
    // make use of `get_unchecked` on slices and avoid a run-time check.
    impl Indexable for [i32; 42] {
        const LEN: usize = 42;
    
        /// See `Indexable` for the safety contract.
        unsafe fn idx_unchecked(&self, idx: usize) -> i32 {
            // SAFETY: As per this trait's documentation, the caller ensures
            // that `idx < 42`.
            unsafe { *self.get_unchecked(idx) }
        }
    }
    
    // The implementation for the never type declares a length of 0,
    // which means `idx_unchecked` can never be called.
    impl Indexable for ! {
        const LEN: usize = 0;
    
        /// See `Indexable` for the safety contract.
        unsafe fn idx_unchecked(&self, idx: usize) -> i32 {
            // SAFETY: As per this trait's documentation, the caller ensures
            // that `idx < 0`, which is impossible, so this is dead code.
            unsafe { std::hint::unreachable_unchecked() }
        }
    }
    
    fn use_indexable<I: Indexable>(x: I, idx: usize) -> i32 {
        if idx < I::LEN {
            // SAFETY: We have checked that `idx < I::LEN`.
            unsafe { x.idx_unchecked(idx) }
        } else {
            panic!("index out-of-bounds")
        }
    }
}
