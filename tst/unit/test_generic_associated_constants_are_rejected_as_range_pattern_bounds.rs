//@ compile-fail: generic parameters
trait Limit { const START: u8; }
fn matches<T: Limit>(value: u8) -> bool {
    match value { T::START..=23 => true, _ => false }
}
impl Limit for () { const START: u8 = 13; }
fn main() { let _ = matches::<()>(17); }
