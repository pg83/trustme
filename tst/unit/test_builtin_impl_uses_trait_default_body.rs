#![feature(transmutability)]

// `TransmuteFrom` has no written impls - the compiler proves it on the fly - so
// the impl behind `<Dst as TransmuteFrom<Src, A>>::transmute` carries no items
// of its own.  The item is the body the trait itself gives it, the same one
// every proved type uses.  Treating the builtin impl as having nothing to
// generate aborted translation on an unknown path.

use std::mem::{Assume, TransmuteFrom};

struct EvenU8 {
    val: u8,
}

fn main() {
    let src: u8 = 42;
    let dst: EvenU8 = unsafe { <_ as TransmuteFrom<_, { Assume::SAFETY }>>::transmute(src) };
    assert_eq!(dst.val, 42);

    let bytes: [u8; 2] = [0xFF, 0xFF];
    let word: u16 = unsafe { <_ as TransmuteFrom<_, { Assume::NOTHING }>>::transmute(bytes) };
    assert_eq!(word, u16::MAX);
}
