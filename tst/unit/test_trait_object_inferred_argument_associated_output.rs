#![feature(unboxed_closures)]
#![allow(dead_code)]

trait Parent<T> {
    type Assoc;
}

trait Child<T>: Parent<T> {}

fn check_local_trait(value: &dyn Child<u32, Assoc = ()>) {
    let inferred: &dyn Child<_, Assoc = ()> = value;
    let _: &dyn Child<u32, Assoc = ()> = inferred;
}

fn check_standard_trait() {
    let _: &mut dyn FnMut<(_,), Output = ()> = &mut |_: ()| {};
}

fn main() {}
