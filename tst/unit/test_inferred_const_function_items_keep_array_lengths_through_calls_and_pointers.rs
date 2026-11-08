#![feature(generic_arg_infer)]
fn identity<const N: usize>(value: [u8; N]) -> [u8; N] { value }
fn apply<const N: usize, F: Fn([u8; N]) -> [u8; N]>(f: F, value: [u8; N]) -> [u8; N] {
    f(value)
}
fn main() {
    let first = identity::<_>;
    let value = apply(first, [2, 3, 5, 7]);
    let second = identity::<_>;
    let pointer: fn([u8; 4]) -> [u8; 4] = second;
    assert_eq!(value, [2, 3, 5, 7]);
    assert_eq!(pointer(value), value);
}
