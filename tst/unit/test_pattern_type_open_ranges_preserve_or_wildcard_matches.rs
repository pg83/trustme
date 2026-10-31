#![feature(pattern_types, pattern_type_macro, const_trait_impl, pattern_type_range_trait)]
use std::pat::pattern_type;
type Split = pattern_type!(i16 is ..=-3 | 3..);
type Window = pattern_type!(u8 is 3..7);
fn choose(choice: bool, value: Split) -> u8 {
    match (choice, value) {
        (true | false, payload) => {
            let base: i16 = unsafe { std::mem::transmute(payload) };
            match base { ..=-3 => 17, 3.. => 23, _ => 41 }
        }
    }
}
fn main() {
    let low: Split = unsafe { std::mem::transmute(-5i16) };
    let high: Split = 7;
    let first: Window = 3;
    let last: Window = 6;
    assert_eq!((choose(false, low), choose(true, high)), (17, 23));
    let first: u8 = unsafe { std::mem::transmute(first) };
    let last: u8 = unsafe { std::mem::transmute(last) };
    assert_eq!((first, last), (3, 6));
}
