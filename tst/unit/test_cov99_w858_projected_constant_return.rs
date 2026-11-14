trait Family {
    type Value;
}

impl Family for () {
    type Value = u8;
}

const BYTE: u8 = 37;

#[inline(never)]
fn projected<T: Family<Value = u8>>() -> T::Value {
    BYTE
}

fn main() {
    assert_eq!(projected::<()>(), 37);
}
