//@ aux-build: aliased_variant_exports.rs
//@ compile-variant: -O
use aliased_variant_exports::{Packet, Blank, MAKE, routed::Create};
const LOCAL: fn(u16) -> Packet<u16> = Create::<u16>;
fn apply<F: FnOnce(u16) -> Packet<u16>>(make: F, value: u16) -> Packet<u16> { make(value) }
fn main() {
    assert!(matches!(LOCAL(23), Packet::Data(23)));
    assert!(matches!(MAKE(29), Packet::Data(29)));
    assert!(matches!(apply(aliased_variant_exports::builder!(u16), 31), Packet::Data(31)));
    assert!(matches!(Blank::<u16>, Packet::Empty));
}
