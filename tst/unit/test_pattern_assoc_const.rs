// A constant pattern `<S as Format<_>>::FORMAT` never passed through the expr
// visitors, so the `_` stayed an unpopulated ivar and aborted typecheck.
trait Format<T> { const FORMAT: T; }
struct S0; struct S1;
impl Format<u8> for S0 { const FORMAT: u8 = 0; }
impl Format<u8> for S1 { const FORMAT: u8 = 1; }
fn f(x: u8) -> u32 {
    match x { S0::FORMAT => 10, S1::FORMAT => 11, _ => 0 }
}
fn main() {
    assert_eq!(f(0), 10);
    assert_eq!(f(1), 11);
    assert_eq!(f(9), 0);
}
