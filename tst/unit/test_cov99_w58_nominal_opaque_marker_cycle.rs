//@ compile-only
//@ compile-flags: -Cpanic=abort
#![feature(type_alias_impl_trait)]

trait Marker {}
impl<T> Marker for T {}

type Opaque = impl Marker;

#[define_opaque(Opaque)]
fn opaque() -> Opaque {
    opaque
}

fn main() {
    let _ = opaque();
}
