//@ compile-variant: -O

enum Choice { Unit, Empty(), Value(u16) }

fn main() {
    let build: fn() -> Choice = Choice::Empty;
    assert!(matches!(build(), Choice::Empty()));
    assert!(matches!(Choice::Unit, Choice::Unit));
    let payload: fn(u16) -> Choice = Choice::Value;
    assert!(matches!(payload(317), Choice::Value(317)));
}
