#![feature(trait_alias)]

trait Source { type Item; fn item() -> Self::Item; }
trait Child: Source {}
trait Unrelated {}
trait Nested = Child;
trait Alias = Unrelated + Nested;

struct Sample;
impl Source for Sample { type Item = u8; fn item() -> u8 { 23 } }
impl Child for Sample {}
impl Unrelated for Sample {}

fn exact<T: Alias<Item = u8>>() -> u8 { T::item() }
fn copy<T: Alias<Item: Copy>>() -> (T::Item, T::Item) {
    let value = T::item();
    (value, value)
}

fn main() {
    assert_eq!(exact::<Sample>(), 23);
    assert_eq!(copy::<Sample>(), (23, 23));
}
