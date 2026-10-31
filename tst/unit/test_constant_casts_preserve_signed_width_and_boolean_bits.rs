fn main() {
    let value = -257i128;
    assert_eq!(value as i8, -1);
    assert_eq!((value as i16, value as i32, value as i64), (-257, -257, -257));
    assert_eq!((value as isize, value as i128), (-257, -257));
    assert_eq!((value as u8, value as u16), (255, 65279));
    assert_eq!(value as u32, u32::MAX - 256);
    assert_eq!(value as u64, u64::MAX - 256);
    assert_eq!(value as u128, u128::MAX - 256);
    assert_eq!(value as usize, usize::MAX - 256);
    let flag = true;
    assert_eq!((flag as i8, flag as u16), (1, 1));
    let character = '\u{10ffff}';
    assert_eq!(character as u32, 0x10ffff);
    let raw = 1u8;
    let restored = unsafe { std::mem::transmute::<u8, bool>(raw) };
    assert_eq!(restored as bool, flag);
}
