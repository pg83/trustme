fn marker() -> u32 { 37 }
fn main() {
    let pointer = marker as *const u16;
    let byte_pointer = marker as *const u8;
    assert!(!pointer.is_null());
    assert_eq!(pointer as usize, byte_pointer as usize);
    assert_eq!(marker(), 37);
}
