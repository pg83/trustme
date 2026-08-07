// Extracted from library/core/src/mem/maybe_uninit.rs:321
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let v: MaybeUninit<String> = MaybeUninit::uninit();
}
