//@ compile-fail: Value mismatch between
struct Packet<const N: usize>([u8; N]);
fn accept(_: Packet<3>) {}
fn main() { accept(Packet::<4>([2, 3, 5, 7])); }
