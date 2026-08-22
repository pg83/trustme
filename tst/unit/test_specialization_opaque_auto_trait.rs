//@ crate-type: lib
#![feature(specialization)]
#![allow(incomplete_features)]

trait Pick: Sized {
    type Output: Default;

    fn pick(self) -> Self::Output {
        Default::default()
    }
}

impl<T> Pick for T {
    default type Output = ();
}

impl<T: Send> Pick for T {
    type Output = bool;
}

trait Foo: std::fmt::Debug + Eq {}

impl<T: std::fmt::Debug + Eq> Foo for T {}

fn hidden<T: Foo>(value: T) -> impl Foo {
    value
}

fn accepted() {
    assert_eq!(Pick::pick(hidden(0_i32)), false);
}
