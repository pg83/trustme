#[repr(i128)]
enum Value {
    Data(u64) = 0,
    Empty = 1,
}

fn main() {
    match Value::Data(7) {
        Value::Data(value) => assert_eq!(value, 7),
        Value::Empty => panic!(),
    }
}
