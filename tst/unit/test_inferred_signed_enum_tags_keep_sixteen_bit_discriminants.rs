//@ compile-variant: -O
#[derive(Clone, Copy, Debug, PartialEq)]
enum Value { Low = -257, Zero = 0, High = 1025 }

#[inline(never)]
fn read(value: Value) -> i16 {
    match value { Value::Low => -257, Value::Zero => 0, Value::High => 1025 }
}

fn main() {
    assert_eq!(std::mem::size_of::<Value>(), 2);
    assert_eq!(std::mem::align_of::<Value>(), 2);
    assert_eq!(std::mem::size_of::<Option<Value>>(), 2);
    for value in [Value::Low, Value::Zero, Value::High] {
        let value = std::hint::black_box(value);
        assert_eq!(read(value), value as i16);
        assert_eq!(Some(value).unwrap(), value);
    }
}
