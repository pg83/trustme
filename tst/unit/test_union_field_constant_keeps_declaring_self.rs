union Packet {
    bytes: [u8; Self::WIDTH],
    word: u32,
}

impl Packet {
    const WIDTH: usize = 3;
}

fn main() {
    let packet = Packet { bytes: [7, 11, 13] };
    unsafe {
        assert_eq!(packet.bytes, [7, 11, 13]);
    }
    assert_eq!(std::mem::size_of::<Packet>(), 4);
}
