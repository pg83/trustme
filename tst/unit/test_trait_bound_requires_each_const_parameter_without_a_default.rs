//@ compile-fail: Omitted value parameter with no default

trait Width<const A: usize, const B: usize> {}

fn inspect<T: Width<3>>() {}

fn main() {}
