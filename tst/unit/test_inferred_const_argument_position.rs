fn make_array<const N: usize, const K: usize>(_: [u8; N]) -> [u8; K] {
    [0; K]
}

#[allow(deprecated)]
fn main() {
    let _: [u8; 2] = make_array::<_, 2>([0; 3]);

    let iter: std::array::IntoIter<u8, 3> = std::array::IntoIter::<u8, _>::new([1, 2, 3]);
    assert_eq!(iter.len(), 3);
}
