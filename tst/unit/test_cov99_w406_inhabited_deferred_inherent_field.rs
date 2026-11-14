//@ crate-type: lib
#![feature(core_intrinsics, type_alias_impl_trait, inherent_associated_types)]
#![allow(incomplete_features)]

type Hidden = impl Sized;

struct Holder<T>(T);
impl<T> Holder<T> {
    type Item = T;
}

struct Envelope(<Holder<Hidden>>::Item);

const CHECK: () = core::intrinsics::assert_inhabited::<Envelope>();

#[define_opaque(Hidden)]
fn hidden_value() -> Hidden { 19u8 }
