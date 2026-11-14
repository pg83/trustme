//@ proc-macro-aux-build: cov99_w281_tuple_enum_attr.rs

use cov99_w281_tuple_enum_attr::keep;

#[keep]
enum Value {
    Tuple(#[doc = "payload"] u8),
}

fn main() {
    let Value::Tuple(value) = Value::Tuple(23);
    assert_eq!(value, 23);
}
