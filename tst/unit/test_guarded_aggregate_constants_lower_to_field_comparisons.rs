#[derive(PartialEq, Eq)]
struct Packet { pair: (u16, bool), bytes: [u8; 3] }
const KEY: Packet = Packet { pair: (37, false), bytes: [2, 5, 8] };
fn classify(value: &Packet, allow: bool) -> u8 {
    match value {
        &KEY if allow => 7,
        Packet { pair: (37, _), bytes: [2, ..] } if allow => 11,
        Packet { pair: (_, false), .. } => 13,
        _ => 17,
    }
}
fn main() {
    assert_eq!(classify(&KEY, true), 7);
    assert_eq!(classify(&KEY, false), 13);
    assert_eq!(classify(&Packet { pair: (37, true), bytes: [2, 0, 0] }, true), 11);
    assert_eq!(classify(&Packet { pair: (1, true), bytes: [0; 3] }, true), 17);
}
