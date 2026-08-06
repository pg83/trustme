// Extracted from library/core/src/mem/maybe_uninit.rs:206
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    assert_eq!(size_of::<MaybeUninit<u64>>(), size_of::<u64>());
    assert_eq!(align_of::<MaybeUninit<u64>>(), align_of::<u64>());
}
