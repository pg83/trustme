static BYTES: [u8; 4] = [37, 41, 43, 47];
#[inline(never)]
fn compare(a: *const u8, b: *const u8) -> [bool; 6] { [a == b, a != b, a < b, a <= b, a > b, a >= b] }
fn main() {
    let first = BYTES.as_ptr();
    let last = unsafe { first.add(3) };
    assert_eq!(compare(first, last), [false, true, true, true, false, false]);
    assert_eq!(compare(last, first), [false, true, false, false, true, true]);
    assert_eq!(compare(first, first), [true, false, false, true, false, true]);
    assert!((&BYTES[0] as *const u8) != (&BYTES[3] as *const u8));
}
