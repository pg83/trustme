mod definitions {
    pub struct Buffer<const N: usize> {
        pub bytes: [u8; N],
    }

    pub type First<const N: usize> = Buffer<N>;
    pub type Second<const N: usize> = First<N>;
}

mod facade {
    pub use super::definitions::Second as Packet;
}

use facade::Packet as Reexported;

fn main() {
    let packet: Reexported<{ 1 + 2 }> = Reexported::<{ 1 + 2 }> {
        bytes: [7, 8, 9],
    };
    assert_eq!(packet.bytes, [7, 8, 9]);
}
