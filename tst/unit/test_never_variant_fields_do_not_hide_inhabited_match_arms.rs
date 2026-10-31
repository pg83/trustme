#![feature(never_type)]
enum Choice { Impossible(!), Value(u8) }
fn select(value: Choice) -> u8 {
    match value { Choice::Impossible(_) => 99, Choice::Value(value) => value }
}
fn main() { assert_eq!(select(Choice::Value(37)), 37); }
