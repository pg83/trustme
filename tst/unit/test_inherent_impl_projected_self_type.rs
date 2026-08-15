//@ crate-type: lib

trait Device {
    type Resources;
}

struct Foo<D, R>(D, R);

impl<D: Device> Foo<D, D::Resources> {}
