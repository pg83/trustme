//@ compile-fail: Choice
struct Choice;
trait Left<T> { fn measure(&self, value: T) -> usize; }
trait Right<T> { fn measure(&self, value: T) -> usize; }
impl Left<u16> for Choice { fn measure(&self, value: u16) -> usize { value as usize } }
impl Right<u16> for Choice { fn measure(&self, value: u16) -> usize { value as usize + 1 } }
fn main() { let choice = Choice; let value = Default::default(); let _ = choice.measure(value); }
