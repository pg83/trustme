//@ compile-variant: -O
struct Unit;
struct Payload(u16);
enum Packet { Empty, Full(u16) }
const UNIT: Unit = Unit;
static EXPOSED: Unit = Unit;
const REFERENCE: &Unit = &Unit;
const STRUCT: fn(u16) -> Payload = Payload;
const VARIANT: fn(u16) -> Packet = Packet::Full;
fn main() {
    assert_eq!(std::mem::size_of_val(&UNIT), 0);
    assert_eq!(std::mem::size_of_val(&EXPOSED), 0);
    let &Unit = REFERENCE;
    assert_eq!(STRUCT(41).0, 41);
    assert!(matches!(VARIANT(43), Packet::Full(43)));
    assert!(matches!(Packet::Empty, Packet::Empty));
}
