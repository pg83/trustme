enum Packet<T> { Pair(T, u8), Named { value: T }, Empty, Zero() }
fn tag<T>(value: Packet<T>) -> u8 {
    match value {
        Packet::Pair { .. } => 1,
        Packet::Named { .. } => 2,
        Packet::Empty {} => 3,
        Packet::Zero {} => 4,
    }
}
struct Unit;
fn main() {
    assert_eq!(tag(Packet::Pair(17u16, 23)), 1);
    assert_eq!(tag(Packet::Named { value: 37u16 }), 2);
    assert_eq!(tag(Packet::<u16>::Empty), 3);
    assert_eq!(tag(Packet::<u16>::Zero()), 4);
    assert_eq!(match Unit { Unit {} => 7 }, 7);
}
