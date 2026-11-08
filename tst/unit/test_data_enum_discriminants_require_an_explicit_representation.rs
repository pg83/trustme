//@ compile-fail: Discrimiant value set on enum with no `repr` set
enum Packet {
    Data(u8) = 3,
}

fn main() {}
