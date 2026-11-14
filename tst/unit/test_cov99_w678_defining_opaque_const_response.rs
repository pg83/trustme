//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(type_alias_impl_trait)]

trait Pick<T, const N: usize> {
    fn pick(&self) -> T;
}

impl Pick<u16, 0> for u8 {
    fn pick(&self) -> u16 {
        *self as u16
    }
}

type Hidden = impl Copy;

#[define_opaque(Hidden)]
fn define(value: Hidden) -> Hidden {
    let _ = <Hidden as Pick<_, 0>>::pick(&value);
    7u8
}
