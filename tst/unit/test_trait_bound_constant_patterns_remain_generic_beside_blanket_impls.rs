//@ compile-fail: generic parameters
trait Pattern { const VALUE: u8; }
impl<T> Pattern for T { const VALUE: u8 = 17; }
fn matches<T: Pattern>(value: u8) -> bool {
    match value { T::VALUE => true, _ => false }
}
fn main() { let _ = matches::<u32>(17); }
