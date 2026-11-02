#![feature(pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]

macro_rules! twice {
    ($body:expr) => {{
        let first = $body;
        assert!($body);
        first
    }};
}

fn main() {
    assert!(twice!({
        let value: std::pat::pattern_type!(u16 is 3..=9) = 5;
        let plain = unsafe { std::mem::transmute::<_, u16>(value) };
        plain == 5
    }));
}
