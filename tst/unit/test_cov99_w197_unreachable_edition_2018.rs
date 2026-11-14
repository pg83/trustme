//@ edition: 2018

fn require_value(value: Option<u8>) -> u8 {
    match value {
        Some(value) => value,
        None => core::unreachable!("w197: absent value"),
    }
}

fn main() {
    assert_eq!(require_value(Some(37)), 37);
}
