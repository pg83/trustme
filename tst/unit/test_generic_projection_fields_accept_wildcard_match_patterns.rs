trait Family { type Value; }
impl Family for u8 { type Value = String; }
struct Envelope<T: Family> { tag: u8, value: T::Value }

fn select<T: Family>(value: Envelope<T>) -> u8 {
    match value {
        Envelope { tag: 0 | 2, value: _ } => 17,
        Envelope { tag: 1, value: _ } => 23,
        Envelope { .. } => 41,
    }
}

fn main() {
    for (tag, expected) in [(0, 17), (1, 23), (2, 17), (3, 41)] {
        assert_eq!(select::<u8>(Envelope { tag, value: String::from("payload") }), expected);
    }
}
