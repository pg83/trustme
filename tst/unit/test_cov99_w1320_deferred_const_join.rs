//@ crate-type: lib

fn make<const N: usize>() -> [u8; N] {
    [0u8; N]
}

fn pass<const N: usize>(value: [u8; N]) -> [u8; N] {
    value
}

pub fn delayed() -> [u8; 3] {
    let value = pass(make());
    let result: [u8; 3] = value;
    result
}
