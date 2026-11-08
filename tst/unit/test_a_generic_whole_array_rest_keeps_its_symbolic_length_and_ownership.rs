//@ compile-fail: Value mismatch between
struct Token(u8);

fn retain<T, const N: usize>(items: [T; N]) -> [T; N] {
    let [rest @ ..] = items;
    rest
}

fn main() {
    let [single] = retain([Token(13)]);
    assert_eq!(single.0, 13);
    let empty: [Token; 0] = retain([]);
    assert_eq!(empty.len(), 0);
}
