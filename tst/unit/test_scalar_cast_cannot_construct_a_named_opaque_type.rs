#![feature(type_alias_impl_trait)]
//@ compile-fail: Invalid cast [dst]

type Hidden = impl Copy;

#[define_opaque(Hidden)]
fn construct() -> Hidden {
    7u16
}

fn forbidden(value: u16) -> Hidden {
    value as Hidden
}

fn main() {}
