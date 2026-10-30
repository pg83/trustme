//@ compile-flags: -Zmir-opt-level=3
static FIRST: u8 = 11;
static SECOND: u8 = 17;
fn compare() -> (bool, bool) {
    let first = &raw const FIRST;
    let same = &raw const FIRST;
    let other = &raw const SECOND;
    (first == same, first == other)
}
fn main() {
    assert_eq!(compare(), (true, false));
    assert_eq!(unsafe { (*(&raw const FIRST), *(&raw const SECOND)) }, (11, 17));
}
