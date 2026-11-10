#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Chunk<const N: usize> {
    bytes: [u8; N],
}

impl<const N: usize> Chunk<N> {
    fn append<const M: usize>(self, suffix: [u8; M]) -> Chunk<{ N + M }>
    where
        [(); N + M]:,
    {
        let mut bytes = [0; N + M];
        bytes[..N].copy_from_slice(&self.bytes);
        bytes[N..].copy_from_slice(&suffix);
        Chunk { bytes }
    }
}

fn concatenate<const L: usize, const R: usize>(left: [u8; L], right: [u8; R]) -> [u8; L + R]
where
    [(); L + R]:,
{
    let combined: Chunk<{ L + R }> = Chunk { bytes: left }.append(right);
    combined.bytes
}

fn main() {
    assert_eq!(concatenate([2, 7, 19], [31, 43]), [2, 7, 19, 31, 43]);
    assert_eq!(concatenate([], [53, 59]), [53, 59]);
    assert_eq!(concatenate([61], []), [61]);
}
