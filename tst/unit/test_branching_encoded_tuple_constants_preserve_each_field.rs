//@ compile-variant: -O
const FIRST: (u16, [u8; 3]) = (613, [3, 5, 7]);
const SECOND: (u16, [u8; 3]) = (617, [11, 13, 17]);
#[inline(never)]
fn choose(flag: bool) -> (u16, [u8; 3]) {
    let result;
    if flag { result = FIRST; } else { result = SECOND; }
    result
}
fn main() {
    assert_eq!(choose(std::hint::black_box(true)), (613, [3, 5, 7]));
    assert_eq!(choose(std::hint::black_box(false)), (617, [11, 13, 17]));
}
