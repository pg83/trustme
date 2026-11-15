#![feature(pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]

use std::pat::pattern_type;

type Bounded = pattern_type!(u64 is 1..=7);

struct Word {
    value: Bounded,
}

fn main() {
    assert_eq!(std::mem::size_of::<Word>(), 8);
    let word = Word {
        value: unsafe { std::mem::transmute::<u64, Bounded>(7) },
    };
    let value: u64 = unsafe { std::mem::transmute(word.value) };
    assert_eq!(value, 7);
}
