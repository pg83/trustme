#[derive(Debug, PartialEq)]
enum Frame<T> { Empty, Data(T), Pair(T, u8) }
fn invoke<T, F: FnOnce(T) -> Frame<T>>(f: F, value: T) -> Frame<T> { f(value) }
fn main() {
    let constructor: fn(u16) -> Frame<u16> = Frame::Data;
    assert_eq!(constructor(37), Frame::Data(37));
    assert_eq!(invoke(Frame::Data, 41u32), Frame::Data(41));
    let pair: fn(u16, u8) -> Frame<u16> = Frame::Pair;
    assert_eq!(pair(43, 5), Frame::Pair(43, 5));
    assert_ne!(constructor(0), Frame::Empty);
}
