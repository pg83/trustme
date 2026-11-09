#![feature(default_field_values)]

enum Entry {
    Empty,
    Value(u8),
}

struct Packet {
    entry: Entry = Entry::Empty {},
    stamp: u8 = 13,
}

fn main() {
    let empty = Packet { .. };
    let full = Packet { entry: Entry::Value(29), .. };
    assert!(matches!(empty.entry, Entry::Empty));
    assert!(matches!(full.entry, Entry::Value(29)));
    assert_eq!((empty.stamp, full.stamp), (13, 13));
}
