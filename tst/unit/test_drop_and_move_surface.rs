//@ crate-type: lib
#![allow(dead_code)]

struct DropValue(i32);
impl Drop for DropValue {
    fn drop(&mut self) {}
}

enum DropEnum {
    A(DropValue),
    B(DropValue, DropValue),
}

fn moves(value: DropEnum, values: Vec<DropValue>) -> DropEnum {
    let (first, second) = (DropValue(1), DropValue(2));
    let DropValue(number) = first;
    let _ = number;

    let mut array = [DropValue(3), DropValue(4)];
    array[0] = DropValue(5);

    match value {
        DropEnum::A(value) => DropEnum::A(value),
        DropEnum::B(left, right) => DropEnum::B(right, left),
    }
}
