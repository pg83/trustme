//@ compile-fail: Unknown receiver type
//@ compile-flags: --emit=metadata -Cpanic=abort

enum Packet<T> {
    Entry(T),
}

struct Owner;

impl Owner {
    fn receive(self: Packet<u8>::Entry) {}
}

fn main() {}
