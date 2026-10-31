fn main() {
    let value = 37u32;
    let original = &value as *const u32;
    let address = original as usize;
    let restored = address as *const u32;
    assert_eq!(restored, original);
    unsafe { assert_eq!(*restored, 37); }
    assert_eq!((-1isize as *const u8) as usize, usize::MAX);
}
