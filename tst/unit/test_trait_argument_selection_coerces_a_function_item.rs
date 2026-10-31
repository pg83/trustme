trait Apply<F> { fn apply(value: u16, f: F) -> u16; }
struct Runner;
impl Apply<fn(u16) -> u16> for Runner {
    fn apply(value: u16, f: fn(u16) -> u16) -> u16 { f(value) }
}
fn increment(value: u16) -> u16 { value + 1 }
fn main() { assert_eq!(Runner::apply(14, increment), 15); }
