//@ compile-variant: -O -Coverflow-checks=off
const INTEGER: u16 = (13u16 * 7 + 11 - 3) / 3 % 17;
const BITS: u16 = (0x5au16 & 0x3f) | (0x21 ^ 0x13);
const SHIFT: u32 = (7u32 << 3u8) >> 1u16;
const FLOAT: f64 = (1.5f64 + 2.5) * 3.0 - 2.0;
fn integer(a: u16, b: u16) -> u16 { (a * b + 11 - 3) / 3 % 17 }
fn bits(a: u16, b: u16) -> u16 { (a & b) | (0x21 ^ 0x13) }
fn shift(a: u32, left: u8, right: u16) -> u32 { (a << left) >> right }
fn float(a: f64, b: f64) -> f64 { (a + b) * 3.0 - 2.0 }
fn main() {
    assert_eq!(INTEGER, integer(13, 7));
    assert_eq!(BITS, bits(0x5a, 0x3f));
    assert_eq!(SHIFT, shift(7, 3, 1));
    assert_eq!(FLOAT, float(1.5, 2.5));
    let mut assigned = 13u16;
    assigned += 2;
    assigned *= 3;
    assigned ^= 7;
    assigned <<= 1u8;
    assigned >>= 2u16;
    assert_eq!(assigned, 21);
    let values = [3u16, 5];
    let pointer = values.as_ptr();
    assert!(pointer == pointer);
    assert!(pointer != unsafe { pointer.add(1) });
    assert!(1.5f64 < 2.5);
    assert!('b' >= 'a');
    assert_eq!(true & false, false);
    assert_eq!(true | false, true);
    assert_eq!(true ^ false, true);
}
