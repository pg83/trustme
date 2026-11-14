//@ aux-build: cov99_w527_value_enum.rs

use cov99_w527_value_enum::Signal;

fn main() {
    let value = Signal::Ready {};
    assert_eq!(value as u8, 17);
}
