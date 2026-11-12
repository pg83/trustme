#![feature(generic_assert)]

fn main() {
    let value = 23u16;
    assert!({
        let value = value + 6;
        value == 29
    });
    assert_eq!(value, 23);
}
