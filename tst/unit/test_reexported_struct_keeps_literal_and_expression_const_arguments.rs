//@ compile-variant: -O
mod source { pub struct Packet<const N: usize>(pub [u8; N]); }
use source::Packet as Imported;
fn main() {
    let literal: Imported<3> = Imported([3, 5, 7]);
    let expression: Imported<{ 2 + 3 }> = Imported([2, 3, 5, 7, 11]);
    assert_eq!((literal.0[2], expression.0[4]), (7, 11));
}
