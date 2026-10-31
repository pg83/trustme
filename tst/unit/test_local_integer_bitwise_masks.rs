fn signed() -> [i16; 6] {
    let left = -37i16;
    let right = 42i16;
    [left & right, left | right, left ^ right, !left, left >> 3u8, right << 2i8]
}
fn unsigned() -> [u16; 6] {
    let left = 219u16;
    let right = 42u16;
    [left & right, left | right, left ^ right, !left, left >> 3u8, right << 2i8]
}
fn main() {
    assert_eq!(signed(), [10, -5, -15, 36, -5, 168]);
    assert_eq!(unsigned(), [10, 251, 241, 65316, 27, 168]);
}
