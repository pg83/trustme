//@ compile-fail: error:
#![feature(generic_const_exprs, pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(incomplete_features)]

const fn length<T, const N: usize>() -> usize { N }

fn project<const N: usize>(_: [u8; N])
    -> [u8; length::<std::pat::pattern_type!(usize is 0..=N), N>()]
where
    [(); length::<std::pat::pattern_type!(usize is 0..=N), N>()]:,
{
    loop {}
}

fn main() {
    let mut value = Default::default();
    value = project(value);
    let _ = value;
}
