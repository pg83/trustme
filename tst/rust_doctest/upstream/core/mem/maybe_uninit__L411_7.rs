// Extracted from library/core/src/mem/maybe_uninit.rs:411
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let mut x = MaybeUninit::<Vec<u8>>::uninit();

    {
        let hello = x.write((&b"Hello, world!").to_vec());
        // Setting hello does not leak prior allocations, but drops them
        *hello = (&b"Hello").to_vec();
        hello[0] = 'h' as u8;
    }
    // x is initialized now:
    let s = unsafe { x.assume_init() };
    assert_eq!(b"hello", s.as_slice());
}
