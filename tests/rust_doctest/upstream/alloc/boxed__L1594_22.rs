// Extracted from library/alloc/src/boxed.rs:1594
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = vec![1, 2, 3].into_boxed_slice();
    let static_ref = Box::leak(x);
    static_ref[0] = 4;
    assert_eq!(*static_ref, [4, 2, 3]);
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    drop(unsafe { Box::from_raw(static_ref) });
}
