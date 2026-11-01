#![feature(box_patterns)]
fn read(value: &Box<(u8, u16)>) -> (u8, u16) {
    let box (first, second) = value;
    (*first, *second)
}
fn main() {
    assert_eq!(read(&Box::new((31, 47))), (31, 47));
}
