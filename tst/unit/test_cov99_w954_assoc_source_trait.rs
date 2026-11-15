//@ crate-type: lib
//@ compile-flags: --emit=dep-info -Z dump-hir
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Width<const N: usize>;

trait ArrayType {
    type Output;
}

impl<const N: usize> ArrayType for Width<N>
where
    [(); N + 1]:,
{
    type Output = [u8; N + 1];
}

trait Parent<T> {
    type Item;
}

trait Child: Parent<<Width<2> as ArrayType>::Output> {}

pub struct Carrier<T: ?Sized>(*const T);

impl Carrier<dyn Child<Item = ()>> {
    pub fn witness() {}
}
