//@ compile-fail: Type alias referenced in generic path doesn't point to a path
#![feature(more_qualified_paths)]
type Identity<T> = T;
fn inspect<T>(value: T) {
    let Identity::<T> {} = value;
}
fn main() { inspect(773u16); }
