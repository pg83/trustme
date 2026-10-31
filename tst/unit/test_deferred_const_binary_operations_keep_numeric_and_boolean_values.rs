//@ compile-flags: -Coverflow-checks=off
fn numeric<const A: u32, const B: u32>() -> [u32; 6] { [A + B, A - B, A * B, A & B, A | B, A ^ B] }
fn flags<const A: bool, const B: bool>() -> (bool, bool, bool) { (A & B, A | B, A ^ B) }
fn shifts<const A: i64, const B: u8>() -> (i64, i64) { (A << B, A >> B) }
fn main() {
    assert_eq!(numeric::<37, 17>(), [54, 20, 629, 1, 53, 52]);
    assert_eq!(numeric::<17, 37>(), [54, 4294967276, 629, 1, 53, 52]);
    assert_eq!((flags::<true, false>(), flags::<true, true>()), ((false, true, true), (true, true, false)));
    assert_eq!(shifts::<-37, 3>(), (-296, -5));
}
