// Extracted from library/std/src/keyword_docs.rs:2052
#![allow(unused)]
fn main() {
    /// # Safety
    ///
    /// `make_even` must return an even number.
    unsafe trait MakeEven {
        fn make_even(&self) -> i32;
    }
    
    // SAFETY: Our `make_even` always returns something even.
    unsafe impl MakeEven for i32 {
        fn make_even(&self) -> i32 {
            self << 1
        }
    }
    
    fn use_make_even(x: impl MakeEven) {
        if x.make_even() % 2 == 1 {
            // SAFETY: this can never happen, because all `MakeEven` implementations
            // ensure that `make_even` returns something even.
            unsafe { std::hint::unreachable_unchecked() };
        }
    }
}
