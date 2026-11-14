#![allow(internal_features)]
#![feature(pattern_types, pattern_type_macro)]

const LOW: bool = false;
const HIGH: bool = true;
type Bit = std::pat::pattern_type!(bool is LOW..=HIGH);

fn main() {
    assert_eq!(std::mem::size_of::<Option<Bit>>(), 1);
}
