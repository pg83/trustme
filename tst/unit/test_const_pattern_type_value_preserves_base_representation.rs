#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u16 is 2..=6);
const VALUE: Small = 5;
fn main() { let value = std::hint::black_box(VALUE); assert_eq!(std::mem::size_of_val(&value),2); let raw: u16 = unsafe { std::mem::transmute(value) }; assert_eq!(raw,5); }
