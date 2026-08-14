#![feature(const_raw_ptr_comparison)]
#![allow(ambiguous_wide_pointer_comparisons)]

use std::mem;
use std::ptr;
use std::str::FromStr;

static BYTE: u8 = 0;

const NULL_EQ: Option<bool> = (0 as *const u8).guaranteed_eq(ptr::null());
const NULL_NE: Option<bool> = (1 as *const u8).guaranteed_eq(ptr::null());
const STATIC_NULL: Option<bool> = (&BYTE as *const u8).guaranteed_eq(ptr::null());
const STATIC_SELF: Option<bool> = (&BYTE as *const u8).guaranteed_eq(&BYTE);

trait Marker {}
impl Marker for u8 {}

fn promoted_size_of() -> &'static usize {
    &mem::size_of::<u8>()
}

fn main() {
    assert_eq!(*promoted_size_of(), 1);
    assert_eq!(NULL_EQ, Some(true));
    assert_eq!(NULL_NE, Some(false));
    assert_eq!(STATIC_NULL, Some(false));
    assert_eq!(STATIC_SELF, None);

    // String iteration compares its provenance-bearing NonNull endpoints
    // while static-borrow promotion evaluates this literal.
    assert_eq!(char::from_str("a").unwrap(), 'a');
    assert_eq!(char::from_str("\0").unwrap(), '\0');
    assert_eq!(char::from_str("\u{D7FF}").unwrap(), '\u{D7FF}');
    assert!(char::from_str("").is_err());
    assert!(char::from_str("abc").is_err());

    let bytes = [1u8, 2];
    let empty = &bytes[..0] as *const [u8];
    let one = &bytes[..1] as *const [u8];
    assert!(empty < one);
    assert!(empty <= one);
    assert!(empty != one);
    assert!(one > empty);
    assert!(one >= empty);

    let a = 1u8;
    let b = 2u8;
    let a = &a as *const dyn Marker;
    let b = &b as *const dyn Marker;
    let a_words: [usize; 2] = unsafe { mem::transmute(a) };
    let b_words: [usize; 2] = unsafe { mem::transmute(b) };
    assert_eq!(a == b, a_words == b_words);
    assert_eq!(a < b, a_words < b_words);
    assert_eq!(a <= b, a_words <= b_words);
    assert_eq!(a > b, a_words > b_words);
    assert_eq!(a >= b, a_words >= b_words);
}
