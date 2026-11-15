//@ edition: 2021
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Probe

trait Bound {}
impl Bound for u8 {}

trait Source {
    type Item;
    fn item(self) -> Self::Item;
}

struct Factory;
impl Source for Factory {
    type Item = u8;
    fn item(self) -> u8 {
        7
    }
}

fn opaque() -> impl Source<Item: Bound> {
    Factory
}

trait Probe {}
impl Probe for u16 {}

fn require_probe<T: Probe>(_: T) {}

fn main() {
    require_probe(opaque().item());
}
