//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Masked<const N: usize> {
    fn bytes(&self) -> [u8; (!N) & 7];
}

fn same<const N: usize>(value: &dyn Masked<N>) -> bool
where
    [(); (!N) & 7]:,
{
    value.bytes() == [1u8; (!N) & 7]
}
