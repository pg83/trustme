const fn echo(values: [u8; { 1 + 2 }]) -> [u8; { 1 + 2 }] {
    values
}
const fn extent() -> usize {
    echo([4, 5, 6]).len()
}
trait Stamp { const TAG: u8; }
impl Stamp for [u8; 3] { const TAG: u8 = 42; }
const RESULT: u8 = <[u8; extent()]>::TAG;
fn main() {
    assert_eq!(RESULT, 42);
}
