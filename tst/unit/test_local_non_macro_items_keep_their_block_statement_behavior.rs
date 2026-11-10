fn main() {
    struct Item(u16);

    impl Item {
        fn new(value: u16) -> Self {
            Self(value)
        }
    }

    fn read(item: Item) -> u16 {
        item.0
    }

    assert_eq!(read(Item::new(47)), 47);
}
