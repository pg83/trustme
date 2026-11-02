#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

macro_rules! twice {
    ($ty:ty) => { (stringify!($ty), stringify!($ty)) };
}

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    let (first, second) = twice!(fn(&'a pin const [u8]) -> &'a pin mut u16);
    let expected = "fn(&'apinconst[u8])->&'apinmutu16";
    assert_eq!(compact(first), expected);
    assert_eq!(compact(second), expected);
}
