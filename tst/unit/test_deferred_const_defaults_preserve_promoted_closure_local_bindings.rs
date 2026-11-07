//@ compile-flags: -Zdump-hir
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn retain<T>(_: &T, width: usize) -> usize {
    width
}

struct Stamp<const N: usize, const M: usize = {
    retain(&|| {
        let local = 293u16;
        local + 2
    }, N)
}>;

impl<const N: usize, const M: usize> Stamp<N, M> {
    fn width() -> usize { M }
}

fn main() {
    assert_eq!(Stamp::<3>::width(), 3);
    assert_eq!(Stamp::<5>::width(), 5);
}
