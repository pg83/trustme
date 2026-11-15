//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub struct Mark<const N: usize>;

pub trait Family<const N: usize> {
    type Item;
}

pub struct Factory;

impl<const N: usize> Family<N> for Factory {
    type Item = Mark<N>;
}

type Via<const N: usize> = <Factory as Family<N>>::Item;

pub fn inferred_alias() -> Mark<7> {
    let value: Via<_> = Mark::<7>;
    value
}
