macro_rules! is_match {
    ($value:expr, $pattern:pat) => {
        matches!($value, $pattern)
    };
}

enum Value {
    Item,
}

fn main() {
    assert!(is_match!(Value::Item, <Value>::Item));
}
