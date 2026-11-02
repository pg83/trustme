//@ compile-variant: -O
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
fn main() {
    let mut value = 17u16;
    assert!(*(&pin const value) == 17);
    assert!({
        let mut pinned = &pin mut value;
        *pinned.as_mut().get_mut() += 6;
        *pinned == 23
    });
    assert_eq!(value, 23);
}
