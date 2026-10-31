#![allow(unconditional_panic)]
#[inline(never)]
fn choose(take: bool) -> i64 {
    if take { let divisor = 0i64; -37 % divisor } else { -41 }
}
fn main() {
    assert_eq!(choose(std::hint::black_box(false)), -41);
}
