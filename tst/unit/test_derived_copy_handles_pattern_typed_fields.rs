#![feature(pattern_types, pattern_type_macro)]
#[derive(Clone, Copy)]
struct Record(std::pat::pattern_type!(u8 is 1..=9));
fn main() {
    let record = Record(unsafe { std::mem::transmute(7u8) });
    let copied = record;
    let value: u8 = unsafe { std::mem::transmute(copied.0) };
    let original: u8 = unsafe { std::mem::transmute(record.0) };
    assert_eq!(value, 7);
    assert_eq!(original, 7);
}
