//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Disagreement on type for
#![feature(type_alias_impl_trait)]

type Callback = impl Copy;

fn identity(value: &u8) -> &u8 {
    value
}

#[define_opaque(Callback)]
fn general_callback() -> Callback {
    identity as for<'a> fn(&'a u8) -> &'a u8
}

#[define_opaque(Callback)]
fn static_callback() -> Callback {
    identity as fn(&'static u8) -> &'static u8
}
