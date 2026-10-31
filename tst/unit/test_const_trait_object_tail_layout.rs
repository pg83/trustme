trait Tag {}
#[repr(align(16))] struct Payload([u8;3]);
impl Tag for Payload {}
#[repr(C)] struct Header<T: ?Sized> { tag: u8, payload: T }
static VALUE: Header<Payload> = Header { tag:7,payload:Payload([1,2,3]) };
const OBJECT: &Header<dyn Tag> = &VALUE;
const SIZE: usize = std::mem::size_of_val(OBJECT);
const ALIGN: usize = std::mem::align_of_val(OBJECT);
fn main() { assert_eq!((SIZE,ALIGN),(32,16)); assert_eq!(OBJECT.tag,7); }
