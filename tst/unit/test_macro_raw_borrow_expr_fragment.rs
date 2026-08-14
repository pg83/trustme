macro_rules! expression {
    ($value:expr) => {
        $value
    };
}

fn main() {
    let values = [1u8, 2, 3];
    let pointer = expression!(&raw const values[1..]);
    assert!(!pointer.is_null());
}
