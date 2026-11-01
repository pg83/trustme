//@ compile-fail: Failed to find an impl
trait Parent<const N:usize> { fn value(&self)->usize; }
trait Child<const N:usize>: Parent<N> {}
fn route<const N:usize,const M:usize>(object: &dyn Child<N>)->usize { <dyn Child<N> as Parent<M>>::value(object) }
fn main() {}
