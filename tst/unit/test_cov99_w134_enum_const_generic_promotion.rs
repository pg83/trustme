enum Single<const N: usize> {
    Value([u8; N]),
}

fn zeroes<const N: usize>() -> &'static Single<N> {
    &Single::Value([0; N])
}

fn main() {
    let Single::Value(bytes) = zeroes::<5>();
    assert_eq!(*bytes, [0; 5]);
}
