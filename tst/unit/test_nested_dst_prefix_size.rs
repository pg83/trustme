// A wrapper's own prefix is only part of an unsized value's size: the tail may
// be another wrapper, whose prefix counts as well.
#![allow(dead_code)]

use std::mem;

const SZ: usize = 100;

struct Wrap<T: ?Sized>([u8; SZ], T);

struct Aligned<T: ?Sized>(u32, T);

fn main() {
    let sized: Box<Wrap<Wrap<[u8; 3]>>> = Box::new(Wrap([0; SZ], Wrap([0; SZ], [0; 3])));
    let expected = mem::size_of_val::<Wrap<Wrap<_>>>(&sized);
    assert_eq!(expected, SZ + SZ + 3);
    let unsized_: Box<Wrap<Wrap<[u8]>>> = sized;
    assert_eq!(mem::size_of_val::<Wrap<Wrap<_>>>(&unsized_), expected);
    assert_eq!(mem::align_of_val::<Wrap<Wrap<_>>>(&unsized_), 1);

    // The tail's alignment raises the wrapper's, so the size rounds up.
    let sized: Box<Aligned<Aligned<[u32; 2]>>> = Box::new(Aligned(1, Aligned(2, [3; 2])));
    let expected = mem::size_of_val::<Aligned<Aligned<_>>>(&sized);
    let unsized_: Box<Aligned<Aligned<[u32]>>> = sized;
    assert_eq!(mem::size_of_val::<Aligned<Aligned<_>>>(&unsized_), expected);
    assert_eq!(mem::align_of_val::<Aligned<Aligned<_>>>(&unsized_), 4);
}
