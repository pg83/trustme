#![feature(pattern_types, pattern_type_macro)]

use std::pat::pattern_type;

type Base = pattern_type!(u8 is 1..=10);
const FIRST: Base = 3;
type Nested = pattern_type!(Base is FIRST..);

struct Wrapper(Nested);

fn main() {
    assert_eq!(core::mem::size_of::<Wrapper>(), 1);
    assert_eq!(core::mem::align_of::<Wrapper>(), 1);
}
