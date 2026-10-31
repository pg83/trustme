fn main() {
    let whole @ 0..=255: u8;
    whole=231;
    assert_eq!(whole,231);
    let (value @ 0 | value @ 1..=255): u8;
    value=19;
    assert_eq!(value,19);
    let (bit @ false | bit @ true): bool;
    bit=true;
    assert!(bit);
}
