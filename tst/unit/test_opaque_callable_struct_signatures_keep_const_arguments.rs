#![feature(type_alias_impl_trait)]

struct Packet<const N: usize> {
    bytes: [u8; N],
}

type Advance<const N: usize> = impl Fn(Packet<N>) -> Packet<N>;

struct Factory;

impl Factory {
    fn keep<const N: usize>(&self, callback: Advance<N>) -> Advance<N> {
        callback
    }

    #[define_opaque(Advance)]
    fn make<const N: usize>(&self) -> Advance<N> {
        self.keep::<N>(|packet| Packet {
            bytes: packet.bytes.map(|byte| byte + 1),
        })
    }
}

fn main() {
    let advance = Factory.make::<3>();
    assert_eq!(advance(Packet { bytes: [7, 31, 127] }).bytes, [8, 32, 128]);
    assert_eq!(Factory.make::<1>()(Packet { bytes: [251] }).bytes, [252]);
}
