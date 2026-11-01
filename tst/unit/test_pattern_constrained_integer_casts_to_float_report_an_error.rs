//@ compile-fail: cast
#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u16 is 2..=6);
fn convert(value: Small) -> f32 { value as f32 }
fn main() {
    let value: Small = 5;
    assert_eq!(convert(value), 5.0);
}
