#![feature(decl_macro)]

mod local {
    pub trait Measure {
        fn measure(&self) -> u16;
    }

    impl Measure for u8 {
        fn measure(&self) -> u16 { *self as u16 + 30 }
    }
}

macro measure($value:expr) {
    crate::local::Measure::measure($value)
}

fn main() {
    assert_eq!(measure!(&12u8), 42);
}
