//@ compile-fail: error

enum Entry {
    Empty,
    Value(u8),
}

fn main() {
    let _entry = Entry::Empty { unexpected: 7u8 };
}
