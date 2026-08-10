enum Item {
    Foo,
    Bar,
    Baz,
}

fn classify(items: &[Option<Item>]) -> u8 {
    match items {
        [Some(Item::Foo), .., Some(Item::Baz | Item::Bar)] => 1,
        [.., Some(Item::Bar | Item::Baz), _] => 2,
        _ => 3,
    }
}

fn matches_cartesian(items: &[Option<Item>]) -> bool {
    match items {
        [Some(Item::Foo | Item::Bar), .., Some(Item::Baz | Item::Bar)] => true,
        _ => false,
    }
}

fn main() {
    let items = [
        Some(Item::Bar),
        Some(Item::Baz),
        Some(Item::Baz),
        Some(Item::Bar),
    ];
    assert_eq!(classify(&items), 2);
    assert!(matches_cartesian(&[Some(Item::Bar), Some(Item::Baz)]));
    assert!(matches_cartesian(&[Some(Item::Foo), Some(Item::Bar)]));
}
