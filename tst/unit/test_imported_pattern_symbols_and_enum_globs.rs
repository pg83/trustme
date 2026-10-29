//@ aux-build: pattern_symbols.rs
fn byte(value: u8) -> u8 {
    use pattern_symbols::{EDGE as edge, LIMIT as limit};
    match value { limit => 11, edge => 12, fresh => fresh }
}
fn endpoint(value: pattern_symbols::End) -> u8 {
    use pattern_symbols::Stop as endpoint;
    match value { endpoint => 13 }
}
fn tag(value: pattern_symbols::Tag) -> u8 {
    use pattern_symbols::Tag::*;
    match value { Unit => 2, Tuple(n) => n, Named { value } => value + 1 }
}
fn bit(value: pattern_symbols::Bit) -> u8 {
    use pattern_symbols::Bit::*;
    match value { Off => 3, On => 8 }
}
fn shadow(value: u8) -> u8 {
    use pattern_symbols::helper;
    match value { helper => helper + 1 }
}
fn main() {
    assert_eq!((byte(5), byte(8), byte(9)), (11, 12, 9));
    assert_eq!(endpoint(pattern_symbols::End), 13);
    assert_eq!(tag(pattern_symbols::Tag::Unit), 2);
    assert_eq!(tag(pattern_symbols::Tag::Tuple(4)), 4);
    assert_eq!(tag(pattern_symbols::Tag::Named { value: 6 }), 7);
    assert_eq!((bit(pattern_symbols::Bit::Off), bit(pattern_symbols::Bit::On)), (3, 8));
    assert_eq!(shadow(12), 13);
}
