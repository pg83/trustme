//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Cov684

#![feature(specialization, rustc_attrs, associated_type_defaults)]
#![allow(internal_features, incomplete_features)]
#![recursion_limit = "32"]

struct Host;
#[rustc_coinductive]
trait Cov684Inner<T> {
    type Item;
    type Fixed = ();
}
impl<T> Cov684Inner<T> for Host {
    default type Item = ();
}
impl<T> Cov684Inner<(T,)> for Host
where
    Host: Cov684Inner<(T,), Fixed = ()>,
{
    type Item = ();
}
trait Cov684Start<T> {}
impl<T> Cov684Start<T> for Host where Host: Cov684Inner<(T,), Item = ()> {}
fn demand<T>() where Host: Cov684Start<T> {}
fn main() {
    demand::<_>();
}
