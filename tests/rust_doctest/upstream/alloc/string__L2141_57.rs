// Extracted from library/alloc/src/string.rs:2141
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = String::from("bucket");
    let static_ref: &'static mut str = x.leak();
    assert_eq!(static_ref, "bucket");
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    drop(unsafe { Box::from_raw(static_ref) });
}
