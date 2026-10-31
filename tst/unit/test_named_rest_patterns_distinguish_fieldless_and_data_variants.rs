enum Packet { Unit, Pair(u8, u8), Record { value: u8 } }
enum Count { First, Second }
#[inline(never)]
fn packet(value: Packet) -> u8 {
    match value { Packet::Unit {} => 3, Packet::Pair { .. } => 5, Packet::Record { .. } => 7 }
}
#[inline(never)]
fn count(value: Count) -> u8 { match value { Count::First {} => 11, Count::Second {} => 13 } }
fn main() {
    assert_eq!(packet(Packet::Unit), 3);
    assert_eq!(packet(Packet::Pair(17, 19)), 5);
    assert_eq!(packet(Packet::Record { value: 23 }), 7);
    assert_eq!((count(Count::First), count(Count::Second)), (11, 13));
}
