//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(deref_patterns, deref_pure_trait, type_alias_impl_trait)]

type Hidden = impl std::ops::Deref<Target = u16> + std::ops::DerefPure;

#[define_opaque(Hidden)]
fn inspect() {
    let deref!(_): Hidden = Box::new(47_u16);
}

fn main() {
    inspect();
}
