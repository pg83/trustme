#![feature(pattern_types, pattern_type_macro)]

#[derive(Clone, later::Copy)]
struct Bounded(std::pat::pattern_type!(u8 is 1..=9));

mod later {
    macro_rules! exports {
        () => { pub use core::clone::Clone; pub use core::marker::Copy; };
    }
    exports!();
}

fn main() {
    let value = Bounded(unsafe { core::mem::transmute(3u8) });
    let copied = value.clone();
    let inner: u8 = unsafe { core::mem::transmute(copied.0) };
    assert_eq!(inner, 3);
}
