//@ compile-fail: Choice
struct Choice;
trait Left { fn measure(&self, value: u16) -> usize; }
trait Right { fn measure(&self, value: u32) -> usize; }
impl Left for Choice { fn measure(&self, value: u16) -> usize { value as usize } }
impl Right for Choice { fn measure(&self, value: u32) -> usize { value as usize + 1 } }
fn main() { let choice = Choice; let value = Default::default(); let _ = choice.measure(value); }
