const fn count() -> usize {
    3
}

struct Packet<const N: usize>([u8; N]);

static PACKET: Packet<{ count() }> = Packet([17, 23, 29]);

enum Tag {
    First = count() as isize,
    Next,
}

const INDEX: usize = Tag::Next as usize - Tag::First as usize;

fn inspect<const N: usize>(packet: &Packet<N>) -> (usize, u8) {
    (packet.0.len(), packet.0[INDEX])
}

fn main() {
    assert_eq!(inspect(&PACKET), (3, 23));
}
