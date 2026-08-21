//@ crate-type: lib

#![feature(impl_trait_in_assoc_type)]

trait Identity {
    type Output;
}

impl<T> Identity for T {
    type Output = T;
}

trait Trait {
    type Inner;
    type Outer: Identity<Output = (Self::Inner,)>;

    fn make() -> Self::Outer;
}

struct Value;

impl Trait for Value {
    type Inner = impl Sized;
    type Outer = impl Identity<Output = (Self::Inner,)>;

    fn make() -> Self::Outer {
        ((),)
    }
}
