struct Tag<const N: usize>;

trait Element {
    type Item;
}

trait Select {
    fn pick() -> u8;
}

impl Select for dyn Element<Item = Tag<3>> {
    fn pick() -> u8 {
        11
    }
}

fn main() {
    let value = <dyn Element<Item = Tag<_>>>::pick();
    assert_eq!(value, 11);
}
