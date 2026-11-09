struct Bytes<const N: u8>;

impl<const N: u8> Bytes<N> {
    const DATA: [u8; 3] = [N, N + 1, N + 2];
    const INNER: &'static [u8; 3] = &Self::DATA;
    const MIDDLE: &'static &'static [u8; 3] = &Self::INNER;
    const OUTER: &'static &'static &'static [u8; 3] = &Self::MIDDLE;
}

#[inline(never)]
fn read<const N: u8>(index: usize) -> u8 {
    Bytes::<N>::OUTER[index]
}

fn main() {
    let index = std::hint::black_box(1);
    assert_eq!(read::<11>(index), 12);
    assert_eq!(read::<41>(index), 42);
    assert_eq!(***Bytes::<11>::OUTER, [11, 12, 13]);
    assert_eq!(***Bytes::<41>::OUTER, [41, 42, 43]);
    assert_eq!(read::<11>(std::hint::black_box(2)), 13);
}
