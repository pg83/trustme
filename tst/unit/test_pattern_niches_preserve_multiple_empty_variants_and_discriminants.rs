#![feature(pattern_types, pattern_type_macro)]
type Digit = std::pat::pattern_type!(u8 is 1..=9);
struct Payload(Digit);
enum Packet { First, Value(Payload), Last }
#[inline(never)]
fn inspect(value: &Packet) -> u8 {
    match value { Packet::First => 37, Packet::Value(Payload(digit)) => unsafe { std::mem::transmute(*digit) }, Packet::Last => 41 }
}
fn main() {
    let first = Packet::First;
    let data = Packet::Value(Payload(unsafe { std::mem::transmute(7u8) }));
    let last = Packet::Last;
    assert_eq!((inspect(&first), inspect(&data), inspect(&last)), (37, 7, 41));
    assert_ne!(std::mem::discriminant(&first), std::mem::discriminant(&data));
    assert_ne!(std::mem::discriminant(&data), std::mem::discriminant(&last));
    assert_ne!(std::mem::discriminant(&first), std::mem::discriminant(&last));
}
