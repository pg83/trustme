//@ compile-variant: -O

enum Choice { Unit, Empty(), Value(u16) }

fn classify(value: Choice) -> u16 {
    match value { Choice::Unit => 3, Choice::Empty() => 7, Choice::Value(value) => value }
}

fn main() {
    assert_eq!(classify(Choice::Empty()), 7);
    assert_eq!(classify(Choice::Unit), 3);
    assert_eq!(classify(Choice::Value(419)), 419);
}
