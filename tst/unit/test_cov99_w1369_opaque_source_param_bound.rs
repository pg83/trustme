//@ edition: 2021
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: error:

trait Bound {}
impl Bound for u8 {}

trait Source {
    type Item;
}

struct Factory;
impl Source for Factory {
    type Item = u8;
}

fn opaque() -> impl Source<Item: Bound> {
    Factory
}

trait Probe<T> {}
impl Probe<u16> for () {}

fn require_source<S: Source>(_: S)
where
    (): Probe<S::Item>,
{}

fn main() {
    require_source(opaque());
}
