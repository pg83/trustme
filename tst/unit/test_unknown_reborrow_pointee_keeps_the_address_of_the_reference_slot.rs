fn main() {
    let bytes = [23u8; 37];
    let nested = &bytes;
    let carried: &_ = &nested;
    let actual = carried as *const _ as usize;
    let expected = &nested as *const _ as usize;
    assert_eq!(actual, expected);
}
