#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Bar<T> {
    type Output;
    fn method() -> Self::Output;
}

trait Foo: for<T> Bar<T, Output = T> {}

fn probe<T: Foo>() {
    let _: i32 = T::method();
}

fn main() {}
