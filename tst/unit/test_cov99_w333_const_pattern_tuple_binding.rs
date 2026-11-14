#![feature(pattern_types, pattern_type_macro)]

type Digit = std::pat::pattern_type!(u16 is 2..=6);

const RAW: u16 = {
    let tuple: (Digit,) = (5,);
    let (value,) = tuple;
    unsafe { std::mem::transmute(value) }
};

fn main() {
    assert_eq!(RAW, 5);
}
