struct Buffer {
    bytes: [u8; Self::CAPACITY],
}

impl Buffer {
    const CAPACITY: usize = 4;
}

fn main() {
    let buffer = Buffer { bytes: [0; Buffer::CAPACITY] };
    assert_eq!(buffer.bytes.len(), 4);
}
