// Indexing an array of length zero is code that never runs - the bounds
// check comes first - but it is still emitted. The C++ type of such an
// array was `{ char _d; }` with no `DATA` member, so the generated
// `arr.DATA[i]` did not compile: indexmap's generic code, instantiated at
// `N = 0`. The type now keeps `DATA[0]` beside `_d` in an anonymous union,
// which leaves its size, alignment and `{0}` initialiser as they were.
#[inline(never)]
fn first_or<const N: usize>(values: [u8; N], fallback: u8) -> u8 {
    if N > 0 { values[0] } else { fallback }
}
#[inline(never)]
fn pick<const N: usize>(values: &[u8; N], index: usize) -> Option<u8> {
    if index < N { Some(values[index]) } else { None }
}
fn main() {
    assert_eq!(first_or([], 9), 9);
    assert_eq!(first_or([4, 5], 9), 4);
    assert_eq!(pick(&[], 0), None);
    assert_eq!(pick(&[1, 2, 3], 2), Some(3));
    let units = [(); 3];
    assert_eq!(units[2], ());
}
