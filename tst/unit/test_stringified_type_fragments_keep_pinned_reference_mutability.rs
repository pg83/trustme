#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(spelling!(&'a pin const [u16])), "&'apinconst[u16]");
    assert_eq!(compact(spelling!(&'b pin mut (u8, u16))), "&'bpinmut(u8,u16)");
}
