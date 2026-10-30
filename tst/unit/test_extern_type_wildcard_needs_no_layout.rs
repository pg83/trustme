#![feature(extern_types)]
extern "C" { type Foreign; }
fn classify(value: Option<&Foreign>) -> u8 {
    match value {
        Some(pointer) => match *pointer { _ => 19 },
        None => 7,
    }
}
fn main() { assert_eq!(classify(None), 7); }
