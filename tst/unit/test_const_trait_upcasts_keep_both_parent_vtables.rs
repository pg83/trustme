//@ compile-variant: -O
trait Left { fn left(&self) -> u16; }
trait Right { fn right(&self) -> u16; }
trait Both: Left + Right {}
struct Number(u16);
impl Left for Number { fn left(&self) -> u16 { self.0 + 3 } }
impl Right for Number { fn right(&self) -> u16 { self.0 + 5 } }
impl Both for Number {}
static NUMBER: Number = Number(17);
const BOTH: &dyn Both = &NUMBER;
const LEFT: &dyn Left = BOTH;
const RIGHT: &dyn Right = BOTH;
const RAW_RIGHT: *const dyn Right = BOTH as *const dyn Both as *const dyn Right;
fn main() {
    assert_eq!(LEFT.left(), 20);
    assert_eq!(RIGHT.right(), 22);
    assert_eq!(unsafe { (&*RAW_RIGHT).right() }, 22);
    assert_eq!(std::mem::size_of_val(RIGHT), 2);
}
