//@ edition: 2021
//@ aux-build: literal_spelling_metadata.rs
//@ proc-macro-aux-build: literal_spelling_relay.rs
extern crate literal_spelling_metadata;
extern crate literal_spelling_relay;

use literal_spelling_relay::spelling_relay;

macro_rules! relay_values {
    ($($value:literal),*) => { spelling_relay!(($($value),*)) };
}

macro_rules! accepts_pattern {
    ($pattern:pat_param, $value:expr) => { matches!($value, $pattern) };
}

fn main() {
    let values = literal_spelling_metadata::spelling_values!(relay_values);
    assert_eq!(values.0, 'λ');
    assert_eq!(values.1, '\'');
    assert_eq!(values.2, '\\');
    assert_eq!(values.3, '\0');
    assert_eq!(values.4, 1.25f32);
    assert_eq!(values.5, 2.5f64);
    assert_eq!(values.6.to_bytes(), b"branch");
    assert!(accepts_pattern!('λ', 'λ'));
    assert!(accepts_pattern!('a'..='z', 'q'));
    assert!(accepts_pattern!(..='m', 'a'));
    assert!(accepts_pattern!('n'.., 'z'));
    assert!(!accepts_pattern!('a'..='z', 'λ'));
}
