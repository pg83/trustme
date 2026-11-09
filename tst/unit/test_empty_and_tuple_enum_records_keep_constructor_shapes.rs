enum Entry {
    Empty,
    Value(u8),
}

fn main() {
    let empty = Entry::Empty {};
    let value = Entry::Value { 0: 23u8 };
    assert!(matches!(empty, Entry::Empty));
    assert!(matches!(value, Entry::Value(23)));
}
