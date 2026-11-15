#![feature(const_trait_impl)]

pub const trait Marker {}

pub const fn source<A: const Marker, B: [const] Marker>(_: &A, _: &B) {}
