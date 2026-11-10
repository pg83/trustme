//@ crate-type: lib

pub fn repeat<T: Copy, const N: usize>(value: T) -> [T; N] {
    [value; N]
}

pub fn first<const N: usize>(values: [u16; N]) -> u16 {
    values[0]
}

pub fn sample() -> [u16; 4] {
    repeat::<u16, 4>(83)
}
