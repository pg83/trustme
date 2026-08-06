// Extracted from library/alloc/src/boxed.rs:1582
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new(41);
    let static_ref: &'static mut usize = Box::leak(x);
    *static_ref += 1;
    assert_eq!(*static_ref, 42);
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    drop(unsafe { Box::from_raw(static_ref) });
}
