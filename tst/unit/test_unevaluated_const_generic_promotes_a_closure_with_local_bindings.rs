#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Packet<const N: usize>
where
    [(); {
        let _operation = &|| {
            let local = 3u16;
            local + 1
        };
        N
    }]:,
{
    bytes: [u8; N],
}

fn main() {
    let packet = Packet::<3> { bytes: [2, 5, 8] };
    assert_eq!(packet.bytes[1], 5);
}
