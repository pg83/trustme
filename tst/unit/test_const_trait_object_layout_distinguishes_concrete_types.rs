trait Tag {}
#[repr(align(32))] struct Block([u8;3]);
impl Tag for Block {}
impl Tag for u16 {}
static BLOCK: Block = Block([1,2,3]);
static NUMBER: u16 = 257;
const OBJECT: &dyn Tag = &BLOCK;
const OTHER: &dyn Tag = &NUMBER;
const SIZE: usize = std::mem::size_of_val(OBJECT);
const ALIGN: usize = std::mem::align_of_val(OBJECT);
const SMALL_SIZE: usize = std::mem::size_of_val(OTHER);
const SMALL_ALIGN: usize = std::mem::align_of_val(OTHER);
fn main() { assert_eq!((SIZE,ALIGN,SMALL_SIZE,SMALL_ALIGN),(32,32,2,2)); }
