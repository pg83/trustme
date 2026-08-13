#[derive(Clone, Copy, PartialEq)]
enum Value {
    First,
    Second,
}

impl Value {
    const DEFAULT: Self = Self::Second;

    fn first() -> Self {
        Self::First
    }
}

fn main() {
    assert!(Value::DEFAULT == Value::Second);
    assert!(Value::first() == Value::First);
}
