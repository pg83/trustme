// Extracted from library/core/src/num/nonzero.rs:91
#![allow(unused)]
fn main() {
    use core::{num::NonZero};

    assert_eq!(size_of::<Option<NonZero<u32>>>(), size_of::<u32>());
}
