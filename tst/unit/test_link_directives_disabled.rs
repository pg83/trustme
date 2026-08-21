//@ compile-flags: -Zlink-directives=no

#[link(name = "trustme-unit-library-that-does-not-exist", kind = "static")]
unsafe extern "C" {}

fn main() {}
