//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(impl_trait_in_bindings)]

trait Local<A, B = Self>: Sized {}

impl Local<u8> for u8 {}

fn main() {
    let _value: impl Local<u8> = 19u8;
}
