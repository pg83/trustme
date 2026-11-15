//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Mark<const N: usize>;
pub struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    pub fn handoff(&self, value: Mark<{ N + 1 }>) -> Mark<{ N + 1 }>
    where
        [(); N + 1]:,
    {
        value
    }
}

pub fn probe<const N: usize>(holder: &Holder<N>, value: Mark<{ N + 1 }>) -> Mark<{ N + 1 }>
where
    [(); N + 1]:,
{
    holder.handoff(value)
}
