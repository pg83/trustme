fn main() {
    let adjustment = 29u16;
    let widen: _ = |byte: u8| -> u16 { adjustment + byte as u16 };
    assert_eq!(widen(13), 42);
}
