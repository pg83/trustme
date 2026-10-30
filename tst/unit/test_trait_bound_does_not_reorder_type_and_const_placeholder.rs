//@ compile-fail: Too many type parameters passed to
trait Width<const N: usize, T> {}

fn accept<X: Width<u8, _>>(_: X) {}

fn main() {}
