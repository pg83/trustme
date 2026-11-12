trait Value {
    fn value(&self) -> u16;
}

impl Value for fn() -> u16 {
    fn value(&self) -> u16 { self() }
}

fn first() -> u16 { 359 }
fn second() -> u16 { 367 }

fn require_value<T: Value>(_: &T) {}

fn main() {
    let chosen = if std::hint::black_box(false) { first } else { second };
    require_value(&chosen);
    let erased: &dyn Value = &chosen;
    assert_eq!(erased.value(), 367);
}
