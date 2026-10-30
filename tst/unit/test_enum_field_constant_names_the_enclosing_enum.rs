enum Message {
    Named { bytes: [u8; Self::WIDTH] },
    Tuple([u8; Self::WIDTH]),
}

impl Message {
    const WIDTH: usize = 3;
}

fn first(value: Message) -> u8 {
    match value {
        Message::Named { bytes } => bytes[0],
        Message::Tuple(bytes) => bytes[2],
    }
}

fn main() {
    assert_eq!(first(Message::Named { bytes: [7; 3] }), 7);
    assert_eq!(first(Message::Tuple([11; 3])), 11);
}
