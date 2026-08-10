// Extracted from library/core/src/mem/maybe_uninit.rs:218
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    assert_eq!(size_of::<Option<bool>>(), 1);
    assert_eq!(size_of::<Option<MaybeUninit<bool>>>(), 2);
}
