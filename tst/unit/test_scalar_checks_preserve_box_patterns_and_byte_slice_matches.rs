#![feature(box_patterns)]

fn main() {
    let bytes: &[u8] = b"ok";
    assert!(matches!(bytes, b"ok"));
    assert!(matches!(Box::new(17u16), box 17u16));
    assert!(matches!(Box::new(23u16), box 20u16..=30u16));
}
