//@ compile-fail: Too many value parameters

trait Width<const N: usize> {}

fn inspect<T: Width<3, 5>>() {}

fn main() {}
