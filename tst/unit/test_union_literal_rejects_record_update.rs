//@ compile-fail: Union constructors can't take a base value
union Packet {
    value: u8,
    tag: u8,
}

fn main() {
    let base = Packet { value: 3 };
    let _ = Packet { tag: 7, ..base };
}
