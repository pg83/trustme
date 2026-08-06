// Extracted from library/alloc/src/vec/mod.rs:2997
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = vec![1, 2, 3];
    let static_ref: &'static mut [usize] = x.leak();
    static_ref[0] += 1;
    assert_eq!(static_ref, &[2, 2, 3]);
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    drop(unsafe { Box::from_raw(static_ref) });
}
