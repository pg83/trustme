//@ crate-type: lib
#![feature(auto_traits, negative_impls)]

auto trait Marker {}

trait Device {
    type Resources;
}

struct Foo<D: Device>(D, D::Resources);

impl<D: Device> !Marker for Foo<D> {}
