#![feature(impl_trait_in_bindings)]

trait Pair<'a, 'b> {}

impl<T> Pair<'_, '_> for T {}

fn same<'a>(_: impl Pair<'a, 'a>) {}

fn check<'a, 'b>(left: &'a u32, right: &'b u32) {
    let value: impl Pair<'_, '_> = (left, right);
    same(value);
}

fn main() {}
