const FIRST: *const u8 = 37usize as *const u8;
const SECOND: *const u8 = 53usize as *const u8;
fn classify(pointer: *const u8) -> u8 {
    match pointer { FIRST => 1, SECOND => 2, _ => 0 }
}
fn main() {
    assert_eq!(classify(FIRST), 1);
    assert_eq!(classify(SECOND), 2);
    assert_eq!(classify(std::ptr::null()), 0);
    assert_eq!(classify(71usize as *const u8), 0);
}
