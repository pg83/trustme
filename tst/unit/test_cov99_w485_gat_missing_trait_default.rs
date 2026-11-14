//@ compile-fail: error:0
//@ compile-only

trait Needs<A> {
    type Assoc;
}

trait Outer {
    type G<T: Needs> where T::Assoc: Sized;
}

fn main() {}
