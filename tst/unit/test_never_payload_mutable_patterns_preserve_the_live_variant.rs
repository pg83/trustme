#![feature(never_type)]
enum Value { Live(u16), Never(!) }
#[inline(never)]
fn update(value: &mut Value) -> usize {
    match value {
        Value::Live(number) => { *number += 7; *number as usize }
        Value::Never(never) => std::mem::size_of_val(never),
    }
}
fn main() {
    let mut value = std::hint::black_box(Value::Live(37));
    assert_eq!(update(&mut value), 44);
    assert_eq!(update(&mut value), 51);
}
