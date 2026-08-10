union Value {
    byte: u8,
    integer: u32,
}

fn main() {
    let value = Value { integer: 7 };
    unsafe {
        let Value { integer } = value;
        assert_eq!(integer, 7);
    }
}
