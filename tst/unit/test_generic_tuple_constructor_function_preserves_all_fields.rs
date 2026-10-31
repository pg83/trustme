#[derive(Debug, PartialEq)]
struct Packet<T>(T, u8);
fn construct<T>(constructor: fn(T, u8) -> Packet<T>, value: T) -> Packet<T> { constructor(value, 7) }
fn main() {
    let constructor = Packet::<u16>;
    let pointer: fn(u16, u8) -> Packet<u16> = constructor;
    assert_eq!(construct(pointer, 23), Packet(23u16, 7));
    assert_eq!(construct(Packet::<u32>, 41), Packet(41u32, 7));
}
