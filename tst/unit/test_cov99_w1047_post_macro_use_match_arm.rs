fn classify(value: bool) -> u8 {
    match value {
        #[macro_use]
        true => 17,
        false => 23,
    }
}

fn main() {
    assert_eq!(classify(true), 17);
    assert_eq!(classify(false), 23);
}
