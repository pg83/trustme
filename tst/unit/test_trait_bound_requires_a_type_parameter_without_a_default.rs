//@ compile-fail: Omitted type parameter with no default

trait Query<T> {}

fn inspect<U: Query>() {}

fn main() {}
