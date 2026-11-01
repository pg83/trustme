//@ compile-variant: -O
#[track_caller]
#[inline(always)]
fn leaf() -> u32 { std::panic::Location::caller().line() }
#[track_caller]
#[inline(always)]
fn forwarded() -> u32 { leaf() }
#[inline(always)]
fn boundary() -> (u32, u32) {
    let expected = line!() + 1;
    (forwarded(), expected)
}
#[track_caller]
fn tracked_wrapper() -> (u32, u32) { boundary() }
fn main() {
    let (actual, expected) = tracked_wrapper();
    assert_eq!(actual, expected);
    let expected = line!() + 1;
    let actual = forwarded();
    assert_eq!(actual, expected);
}
