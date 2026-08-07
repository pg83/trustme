#[repr(i128)]
enum Value {
    Data(u64) = 0,
    Empty = 1,
}

fn is_data(value: &Value) -> bool {
    match value {
        Value::Data(_) => true,
        Value::Empty => false,
    }
}

fn main() {
    assert!(is_data(&Value::Data(7)));
    assert!(!is_data(&Value::Empty));
}
