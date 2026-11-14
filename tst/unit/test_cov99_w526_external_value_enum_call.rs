//@ aux-build: cov99_w526_value_enum.rs

use cov99_w526_value_enum::Pulse;

fn main() {
    assert_eq!(Pulse::Ready(), 53);
}
