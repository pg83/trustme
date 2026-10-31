enum Packet { Data([u8; 257]), Empty }
#[inline(never)]
fn inspect(value: Packet) -> u8 {
    match value { Packet::Data([first, .., last]) => first + last, Packet::Empty => 0 }
}
#[inline(never)]
fn ignore(value: Packet) -> bool { match value { Packet::Data(_) => true, Packet::Empty => false } }
fn main() {
    let mut data = [0; 257];
    data[0] = 3;
    data[256] = 37;
    assert_eq!(inspect(Packet::Data(data)), 40);
    assert_eq!(inspect(Packet::Empty), 0);
    assert!(ignore(Packet::Data(data)));
    assert!(!ignore(Packet::Empty));
}
