#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Bar<T> {
    fn method() -> T;
}

trait Foo: for<T> Bar<T> {}

fn probe<T: Foo>() {
    let _: i32 = T::method();
}

fn main() {}
