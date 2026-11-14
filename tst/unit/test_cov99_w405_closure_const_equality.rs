//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn width<T>() -> usize {
    std::mem::size_of::<T>()
}

fn preserve<F>(_: F, value: [u8; width::<F>()]) -> [u8; width::<F>()]
where
    [(); width::<F>()]:,
{
    value
}

fn main() {
    let closure = || 17_u8;
    let _: [u8; 0] = preserve(closure, []);
}
