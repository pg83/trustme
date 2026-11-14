//@ crate-type: lib
//@ compile-flags: -Z dump-mir

pub fn repeat<const N: usize>(value: u8, choice: bool) -> [u8; N] {
    if choice {
        [value; N]
    } else {
        [value; { N }]
    }
}
