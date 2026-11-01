struct Packet<T>(T, u8, u16, u32);
enum Event<T> { Ready(T, u8, u16, u32), Empty }
fn packet(value: Packet<(u8, u16, u32)>) -> (u8, u32) {
    let Packet((first, ..), .., last) = value;
    (first, last)
}
fn event(value: Event<(u8, u16, u32)>) -> u32 {
    match value {
        Event::Ready((.., last), .., end) => last + end,
        Event::Empty => 0,
    }
}
fn main() {
    assert_eq!(packet(Packet((2, 3, 5), 7, 11, 13)), (2, 13));
    assert_eq!(event(Event::Ready((2, 3, 5), 7, 11, 13)), 18);
    assert_eq!(event(Event::Empty), 0);
}
