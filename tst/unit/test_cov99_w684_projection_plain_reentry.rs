//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

#![feature(specialization, rustc_attrs)]
#![allow(internal_features, incomplete_features)]
#![recursion_limit = "32"]

struct Host;
#[rustc_coinductive]
trait Cov684Inner<T: ?Sized> {
    type Item;
}
impl<T: ?Sized> Cov684Inner<T> for Host {
    default type Item = ();
}
impl<T: ?Sized> Cov684Inner<(T,)> for Host
where
    Host: Cov684Inner<(T,)>,
{
    type Item = ();
}
fn main() {
    let _: <Host as Cov684Inner<(_,)>>::Item = ();
}
