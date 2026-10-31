const NIL: *const u8 = std::ptr::null();
fn classify(pointer: *const u8, allow: bool) -> u8 {
    match pointer { NIL if allow => 7, NIL => 11, _ => 13 }
}
fn main() {
    assert_eq!(classify(std::ptr::null(), true), 7);
    assert_eq!(classify(std::ptr::null(), false), 11);
    assert_eq!(classify(17usize as *const u8, true), 13);
}
