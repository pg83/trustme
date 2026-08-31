//@ check-pass
//@ compile-flags: -Znext-solver

use core::mem::MaybeUninit;

fn compare_buffer_pointer(buffer: &mut [MaybeUninit<u8>]) {
    let previous = buffer as *const _;
    assert!(core::ptr::addr_eq(previous, buffer));
}

fn main() {
    let mut storage = [MaybeUninit::uninit(); 4];
    compare_buffer_pointer(&mut storage);
}
