//@ compile-fail: Failed to find an impl
trait Parent<T> { fn value(&self, input:T)->T; }
trait Child<T>: Parent<T> {}
fn route<T,U>(object: &dyn Child<T>, input:U)->U { <dyn Child<T> as Parent<U>>::value(object,input) }
fn main() {}
