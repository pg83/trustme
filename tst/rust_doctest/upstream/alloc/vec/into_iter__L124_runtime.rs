// Extracted from library/alloc/src/vec/into_iter.rs:124
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::<u8>::with_capacity(10);
    let ptr = vec.as_mut_ptr();
    let mut into_iter = vec.into_iter();
    let mut into_iter = std::mem::replace(&mut into_iter, Vec::new().into_iter());
    (&mut into_iter).for_each(drop);
    std::mem::forget(into_iter);
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    drop(unsafe { Vec::<u8>::from_raw_parts(ptr, 0, 10) });
}
