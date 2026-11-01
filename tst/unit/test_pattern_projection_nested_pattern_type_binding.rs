#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u16 is 2..=6);
const VALUE: Small = 5;
fn main() {
    let value = std::hint::black_box((3u8, VALUE));
    let selected = match value { (3, item) => unsafe { std::mem::transmute::<Small,u16>(item) }, _ => 0 };
    assert_eq!(selected, 5);
}
