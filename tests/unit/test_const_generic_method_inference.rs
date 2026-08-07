fn main() {
    let values = ['l', 'o', 'r', 'e', 'm'];
    let (remainder, chunks) = values.as_rchunks();

    assert_eq!(remainder, &['l']);
    assert_eq!(chunks, &[['o', 'r'], ['e', 'm']]);
}
