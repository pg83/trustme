//@ compile-fail: Enum literals cannot have a base value
enum Packet {
    Data { value: u8, tag: u8 },
}

fn main() {
    let base = Packet::Data { value: 3, tag: 5 };
    let build = || Packet::Data { value: 7, ..base };
    let _ = build();
}
