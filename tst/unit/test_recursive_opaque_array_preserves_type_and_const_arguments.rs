fn keep<T: Copy, const N: usize>(value: [T; N], recurse: bool) -> impl Copy + AsRef<[T]> {
    if recurse {
        let nested: [T; N] = keep(value, false);
        return nested;
    }
    value
}

fn main() {
    let words = keep([13u16, 17, 19], true);
    let bytes = keep([23u8; 5], true);
    assert_eq!(words.as_ref(), &[13, 17, 19]);
    assert_eq!(bytes.as_ref(), &[23, 23, 23, 23, 23]);
}
