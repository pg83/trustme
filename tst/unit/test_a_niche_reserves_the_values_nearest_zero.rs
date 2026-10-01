// Upstream keeps one valid range per scalar and reserves an enum's niche
// values next to it (`Niche::reserve`), towards zero when it can, so an
// `Option` of an `Option` still finds values left. We stored `Option<E>` with
// the zero shortcut that no outer enum could reuse, and `Option<Option<E>>`
// grew a byte.
#![allow(dead_code)]
use std::mem::size_of;

#[derive(Clone, Copy)]
#[repr(u8)]
enum E2 {
    A = 1,
    B = 2,
}

#[derive(Clone, Copy)]
#[repr(u8)]
enum H3 {
    A = 1,
    B = 2,
    C = 3,
}

fn bytes<T>(value: &T) -> Vec<u8> {
    let start = value as *const T as *const u8;
    (0..size_of::<T>()).map(|i| unsafe { *start.add(i) }).collect()
}

fn main() {
    assert_eq!(size_of::<Option<Option<E2>>>(), 1);
    assert_eq!(bytes(&None::<Option<H3>>), [4]);
    assert_eq!(bytes(&Some(None::<H3>)), [0]);
    assert_eq!(bytes(&Some(Some(H3::B))), [2]);
    assert_eq!(size_of::<Option<Option<Option<&u8>>>>(), 16);
}
