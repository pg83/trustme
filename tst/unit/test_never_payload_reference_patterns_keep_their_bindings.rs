#![feature(never_type)]
enum Value { Live(u16), Never(!) }
#[inline(never)]
fn inspect(value: &Value) -> usize {
    match value {
        Value::Live(number) => *number as usize,
        Value::Never(never) => std::mem::size_of_val(never),
    }
}
fn main() {
    assert_eq!(inspect(&std::hint::black_box(Value::Live(37))), 37);
}
