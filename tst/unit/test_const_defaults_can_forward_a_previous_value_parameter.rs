struct Bytes<const N: usize, const M: usize = N>([u8; M]);

fn make<const N: usize>() -> Bytes<N> {
    Bytes([59; N])
}

fn main() {
    let first = make::<3>();
    let second = make::<5>();
    assert_eq!(first.0, [59; 3]);
    assert_eq!(second.0, [59; 5]);
}
