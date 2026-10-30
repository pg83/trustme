//@ compile-fail: Enum literals cannot have a base value
enum Packet {
    Data { value: u8, tag: u8 },
}

const fn length() -> usize {
    let base = Packet::Data { value: 3, tag: 5 };
    let _ = Packet::Data { value: 7, ..base };
    3
}

const VALUE: [u8; length()] = [0; 3];

fn main() {}
