//@ crate-type: lib
//@ compile-fail: Type mismatch
#![feature(type_alias_impl_trait)]

type Hidden = impl Copy;

#[define_opaque(Hidden)]
fn make_hidden() -> Hidden {
    7u8
}

mod markers {
    pub struct Marker;
}

use markers as renamed;

type Plain = u8;
struct Unit;

#[define_opaque(renamed::Marker, Plain, Unit)]
fn invalid_definition() -> Hidden {
    3u8
}
