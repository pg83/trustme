// A const parameter stands on its own in an array length; computing with one
// takes a feature this file does not enable. What this checks is that the forms
// that do not compute still work.
const K: usize = 3;

fn bare<const N: usize>() -> [u8; N] {
    [1; N]
}

fn braced<const N: usize>() -> [u8; { N }] {
    [2; { N }]
}

fn from_const() -> [u8; K + 1] {
    [3; K + 1]
}

fn main() {
    assert_eq!(bare::<2>().len(), 2);
    assert_eq!(braced::<4>().len(), 4);
    assert_eq!(from_const().len(), 4);
}
