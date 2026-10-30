//@ compile-fail: Too many type parameters passed to
trait Count<T> {}

fn accept<T: Count<u8, u16>>(_: T) {}

fn main() {}
