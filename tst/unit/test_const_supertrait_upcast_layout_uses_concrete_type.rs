trait Left { fn left(&self)->u8; }
trait Right { fn right(&self)->u8; }
trait Child: Left + Right {}
#[repr(align(16))] struct Payload([u8;3]);
impl Left for Payload { fn left(&self)->u8 { self.0[0] } }
impl Right for Payload { fn right(&self)->u8 { self.0[2] } }
impl Child for Payload {}
static VALUE: Payload = Payload([3,5,7]);
const CHILD: &dyn Child = &VALUE;
const LEFT: &dyn Left = CHILD;
const RIGHT: &dyn Right = CHILD;
const LAYOUT: (usize,usize,usize,usize) = (std::mem::size_of_val(LEFT),std::mem::align_of_val(LEFT),std::mem::size_of_val(RIGHT),std::mem::align_of_val(RIGHT));
fn main() { assert_eq!(LAYOUT,(16,16,16,16));assert_eq!(LEFT.left(),3);assert_eq!(RIGHT.right(),7); }
