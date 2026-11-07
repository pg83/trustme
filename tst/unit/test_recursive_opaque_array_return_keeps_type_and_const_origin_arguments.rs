fn retain<T: Copy, const N: usize>(value: [T; N], again: bool) -> impl AsRef<[T]> + Copy {
    if !again {
        return value;
    }
    retain::<T, N>(value, false)
}

fn main() {
    let value = retain([263u16; 3], true);
    assert_eq!(value.as_ref(), &[263u16; 3]);
}
