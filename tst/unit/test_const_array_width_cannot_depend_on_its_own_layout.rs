//@ compile-fail: error:0:

const WIDTH: usize = packet_width();

struct Packet {
    payload: [u8; WIDTH],
}

const fn packet_width() -> usize {
    core::mem::size_of::<Packet>()
}

fn main() {}
