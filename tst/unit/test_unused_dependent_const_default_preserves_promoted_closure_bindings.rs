//@ compile-flags: -Zdump-hir
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn retain<T>(_: &T, width: usize) -> usize {
    width
}

struct Mark<const N: usize, const M: usize = {
    retain(&|| {
        let local = 307u16;
        local + 4
    }, N)
}>;

fn main() {
    assert_eq!(std::mem::size_of::<Mark<3, 5>>(), 0);
}
