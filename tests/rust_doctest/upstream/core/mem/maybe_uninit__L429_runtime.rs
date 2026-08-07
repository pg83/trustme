// Extracted from library/core/src/mem/maybe_uninit.rs:429
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let mut x = MaybeUninit::<String>::uninit();

    x.write("Hello".to_string());
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    unsafe { MaybeUninit::assume_init_drop(&mut x); }
    // This leaks the contained string:
    x.write("hello".to_string());
    // x is initialized now:
    let s = unsafe { x.assume_init() };
}
