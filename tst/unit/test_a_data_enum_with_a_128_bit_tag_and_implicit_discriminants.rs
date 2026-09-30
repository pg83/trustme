// A `#[repr(i128)]` data enum whose discriminants are its variants' indices
// was given index tags, and the tag code wrote them as plain integers into
// the emulated 128-bit tag. Its tags are the discriminants, written as the
// 128-bit values they are, like those of an enum that spells them out.
#[repr(i128)]
enum Value {
    Data(u64),
    Empty,
}

fn main() {
    match Value::Data(7) {
        Value::Data(value) => assert_eq!(value, 7),
        Value::Empty => panic!(),
    }
    assert!(matches!(Value::Empty, Value::Empty));
}
