enum Frame<T> { Pair(T, bool), Empty }
fn classify(value: Frame<u16>, allow: bool) -> u8 {
    match value { Frame::Pair(37, false) if allow => 7, Frame::Pair(_, false) => 11, Frame::Empty => 13, _ => 17 }
}
fn main() {
    assert_eq!(classify(Frame::Pair(37, false), true), 7);
    assert_eq!(classify(Frame::Pair(37, false), false), 11);
    assert_eq!(classify(Frame::Pair(41, false), true), 11);
    assert_eq!(classify(Frame::Pair(37, true), true), 17);
    assert_eq!(classify(Frame::Empty, true), 13);
}
