//@ compile-fail: constant evaluation
//@ compile-flags: --emit=metadata

#![feature(rustc_attrs)]

struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: usize = *&pattern_value();
}

#[rustc_promotable]
const fn pattern_value() -> usize {
    match 0 {
        Holder::<0>::VALUE => 1,
        _ => 0,
    }
}

const TRIGGER: usize = Holder::<0>::VALUE;

fn main() {}
