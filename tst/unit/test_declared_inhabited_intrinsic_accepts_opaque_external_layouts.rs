#![feature(intrinsics, extern_types)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn assert_inhabited<T: ?Sized>();

extern "C" { type Foreign; }

const CHECK: () = assert_inhabited::<Foreign>();

fn main() {
    assert_eq!(CHECK, ());
    assert_eq!(std::mem::size_of::<*const Foreign>(), std::mem::size_of::<usize>());
}
