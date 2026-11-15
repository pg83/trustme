//@ compile-flags: -C opt-level=1
#![feature(core_intrinsics)]
#![allow(internal_features)]

#[inline(never)]
fn reference_size<T: ?Sized>(_: &T) -> usize {
    core::intrinsics::size_of::<&T>()
}

fn main() {
    let word = core::mem::size_of::<usize>();
    assert_eq!(reference_size(&7u8), word);
    assert_eq!(reference_size("abc"), 2 * word);
}
