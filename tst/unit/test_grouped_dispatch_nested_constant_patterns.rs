#[derive(PartialEq, Eq)]
enum Kind { Left(u8), Right { code: u16 } }
#[derive(PartialEq, Eq)]
struct Packet { kind: Kind, bytes: [u8; 2] }
const A: Packet = Packet { kind: Kind::Left(3), bytes: [5, 7] };
const B: Packet = Packet { kind: Kind::Right { code: 257 }, bytes: [11, 13] };
fn classify(packet: &Packet) -> u8 {
    match packet {
        &A => 1,
        &B => 2,
        Packet { kind: Kind::Left(3), bytes: [5, _] } => 3,
        Packet { kind: Kind::Right { code: 257 }, .. } => 4,
        _ => 0,
    }
}
fn main() {
    assert_eq!(classify(&A), 1);
    assert_eq!(classify(&B), 2);
    assert_eq!(classify(&Packet { kind: Kind::Left(3), bytes: [5, 9] }), 3);
    assert_eq!(classify(&Packet { kind: Kind::Right { code: 257 }, bytes: [0, 0] }), 4);
    assert_eq!(classify(&Packet { kind: Kind::Left(4), bytes: [5, 7] }), 0);
}
