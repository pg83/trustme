//@ aux-build: impl_row_contract.rs
extern crate impl_row_contract;
use impl_row_contract::Rows;

struct Seed(u16);
macro_rules! rows {
    () => {
        type Row<const N: usize> = [u16; N];
        const BIAS: u16 = 5;
        fn row<const N: usize>(&self) -> Self::Row<N> {
            [self.0 + Self::BIAS; N]
        }
    };
}
impl Rows for Seed { rows!(); }

fn main() {
    assert_eq!(Seed(8).row::<2>(), [13, 13]);
    assert_eq!(Seed::BIAS, 5);
}
