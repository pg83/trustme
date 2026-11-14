//@ compile-fail: auto traits cannot have generic parameters
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(auto_traits)]

auto trait Marker<T> {}

fn differing(source: &dyn Marker<u16>) {
    let _: &dyn Marker<u8> = source;
}

fn main() {}
