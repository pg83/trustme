//@ compile-fail: Choice
struct Choice;
trait Left { fn route<T: Copy>(&self, value: T) -> T; }
trait Right { fn route<T: Copy>(&self, value: T) -> T; }
impl Left for Choice { fn route<T: Copy>(&self, value: T) -> T { value } }
impl Right for Choice { fn route<T: Copy>(&self, value: T) -> T { value } }
fn main() { let choice = Choice; let value = Default::default(); let result = choice.route(value); let _: u16 = result; }
