enum Slot<T> { Empty, Full(T) }
const WORD: fn(u16) -> Slot<u16> = Slot::Full;
const PAIR: fn((u8, u16)) -> Slot<(u8, u16)> = Slot::Full;
const TABLE: [fn(u16) -> Slot<u16>; 2] = [Slot::Full, WORD];

fn take<T>(value: Slot<T>) -> T {
    match value { Slot::Full(value) => value, Slot::Empty => panic!() }
}
#[inline(never)]
fn call(make: fn(u16) -> Slot<u16>, value: u16) -> u16 { take(make(value)) }

fn main() {
    assert_eq!(take(WORD(17)), 17);
    assert_eq!(take(PAIR((3, 19))), (3, 19));
    assert_eq!(take(TABLE[1](23)), 23);
    assert_eq!(call(WORD, 29), 29);
    assert_eq!(call(Slot::Full, 31), 31);
    assert!(matches!(Slot::<u16>::Empty, Slot::Empty));
}
