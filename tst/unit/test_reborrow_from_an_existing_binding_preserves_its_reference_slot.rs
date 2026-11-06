fn main() {
    let bytes = [29u8; 37];
    let nested = &bytes;
    let source = &nested;
    let carried: &_ = source;
    let actual = carried as *const _ as usize;
    let expected = &nested as *const _ as usize;
    assert_eq!(actual, expected);
}
