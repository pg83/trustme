#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};

#[repr(C)]
struct Checked<'a> { tag: bool, count: u16, payload: &'a u16 }
#[repr(C)]
struct Bytes<'a> { tag: u8, count: u16, payload: &'a u16 }
#[repr(C)]
struct Writable<'a> { tag: bool, count: u16, payload: &'a mut u16 }

fn main() {
    let value = 513u16;
    let checked = Checked { tag: true, count: 29, payload: &value };
    let bytes = unsafe { <Bytes<'_> as TransmuteFrom<Checked<'_>, { Assume::SAFETY }>>::transmute(checked) };
    assert_eq!(bytes.tag, 1);
    assert_eq!(bytes.count, 29);
    assert_eq!(*bytes.payload, 513);
    assert!(std::ptr::eq(bytes.payload, &value));

    let checked = unsafe { <Checked<'_> as TransmuteFrom<Bytes<'_>, { Assume::SAFETY.and(Assume::VALIDITY) }>>::transmute(bytes) };
    assert!(checked.tag);
    assert_eq!(*checked.payload, 513);

    let mut other = 144u16;
    let writable = Writable { tag: false, count: 7, payload: &mut other };
    let bytes = unsafe { <Bytes<'_> as TransmuteFrom<Writable<'_>, { Assume::SAFETY }>>::transmute(writable) };
    assert_eq!(bytes.tag, 0);
    assert_eq!(bytes.count, 7);
    assert_eq!(*bytes.payload, 144);
}
