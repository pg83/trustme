//@ compile-fail: Unsupported path in #[define_opaque]
#![feature(no_core, type_alias_impl_trait)]
#![no_core]

trait Carrier {
    type Item;
}

#[define_opaque(<u8 as Carrier>::Item)]
fn define() {}

fn main() {}
