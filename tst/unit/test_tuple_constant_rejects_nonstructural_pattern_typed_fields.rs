//@ compile-fail: constant of non-structural pattern type
#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u8 is 1..=9);
const SEVEN: (Small,) = (unsafe { core::mem::transmute::<u8, Small>(7) },);

fn small(value: (Small,)) -> bool {
    matches!(value, SEVEN)
}

fn main() {
    assert!(small((unsafe { core::mem::transmute::<u8, Small>(7) },)));
    assert!(!small((unsafe { core::mem::transmute::<u8, Small>(5) },)));
}
