#[derive(PartialEq, Eq)]
enum Item<T> { Missing, Present { tag: bool, value: T } }
const KEY: Item<(u8, [u16; 2])> = Item::Present { tag: true, value: (17, [31, 53]) };
fn classify(value: Item<(u8, [u16; 2])>) -> bool { matches!(value, KEY) }
fn main() {
    assert!(classify(Item::Present { tag: true, value: (17, [31, 53]) }));
    assert!(!classify(Item::Present { tag: false, value: (17, [31, 53]) }));
    assert!(!classify(Item::Present { tag: true, value: (17, [31, 59]) }));
    assert!(!classify(Item::Missing));
}
