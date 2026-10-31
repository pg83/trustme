enum Message { Empty, Packet(u8) }
#[inline(never)]
fn select(fallback: Message) -> u8 {
    let first = Message::Packet(23);
    let second = first;
    let third = second;
    match (fallback, third) {
        (Message::Empty, Message::Packet(value)) => value,
        (Message::Packet(extra), Message::Packet(value)) => value + extra,
        _ => 0,
    }
}
fn main() {
    assert_eq!(select(Message::Empty), 23);
    assert_eq!(select(Message::Packet(17)), 40);
}
