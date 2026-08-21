//@ crate-type: lib

#![feature(type_alias_impl_trait)]

use std::ops::Deref;

trait Pick {}

impl<A: Deref, B: Deref<Target = A::Target>> Pick for (A, B, u8) {}
impl<A, B> Pick for (A, B, i8) {}

type Hidden = impl Sized;

#[define_opaque(Hidden)]
fn define() -> Hidden {}

fn require<T: Pick>() {}

fn check() {
    require::<(&Hidden, &String, _)>();
}
