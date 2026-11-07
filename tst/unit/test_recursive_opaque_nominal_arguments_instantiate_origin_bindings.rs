#[derive(Clone, Copy)]
struct Packet<T: Copy, const N: usize>([T; N]);

impl<T: Copy, const N: usize> AsRef<[T]> for Packet<T, N> {
    fn as_ref(&self) -> &[T] {
        &self.0
    }
}

#[inline(never)]
fn relay<T: Copy, const N: usize>(packet: Packet<T, N>, depth: u8) -> impl AsRef<[T]> + Copy {
    if depth == 0 {
        return packet;
    }
    relay(packet, depth - 1)
}

fn main() {
    let first = relay(Packet([773u16; 3]), 2);
    let second = relay(Packet([787u32; 2]), 1);
    assert_eq!(first.as_ref(), &[773u16; 3]);
    assert_eq!(second.as_ref(), &[787u32; 2]);
}
