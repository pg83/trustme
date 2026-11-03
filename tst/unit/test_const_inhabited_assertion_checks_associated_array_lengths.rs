#![feature(core_intrinsics, generic_const_exprs)]
#![allow(incomplete_features)]

trait Width { const N: usize; }
struct Twice<const N: usize>;
impl<const N: usize> Width for Twice<N> { const N: usize = N * 2; }
enum Void {}

const fn check<T: Width>() where [(); T::N]:, [(); T::N + 1]: {
    core::intrinsics::assert_inhabited::<[Void; T::N]>();
    core::intrinsics::assert_inhabited::<[u16; T::N + 1]>();
}

const CHECK: () = check::<Twice<0>>();

fn main() { assert_eq!(CHECK, ()); }
