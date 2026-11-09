#![feature(specialization)]
#![allow(incomplete_features)]

trait Label {
    fn label(&self) -> u8;
}

impl<T> Label for T {
    default fn label(&self) -> u8 {
        31
    }
}

impl Label for u16 {
    fn label(&self) -> u8 {
        37
    }
}

fn main() {
    assert_eq!(5u8.label(), 31);
    assert_eq!(7u16.label(), 37);
}
