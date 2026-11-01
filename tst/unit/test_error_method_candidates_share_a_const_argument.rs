//@ compile-fail: Choice
struct Choice;
trait Left<const N: usize> { fn measure(&self, value: [u8; N]) -> usize; }
trait Right<const N: usize> { fn measure(&self, value: [u8; N]) -> usize; }
impl Left<3> for Choice { fn measure(&self, value: [u8; 3]) -> usize { value.len() } }
impl Right<3> for Choice { fn measure(&self, value: [u8; 3]) -> usize { value.len() + 1 } }
fn main() { let choice = Choice; let value = [7; _]; let _ = choice.measure(value); }
