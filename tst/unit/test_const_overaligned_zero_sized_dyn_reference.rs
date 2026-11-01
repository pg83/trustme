trait Marker {}
#[repr(align(8192))]
struct Empty;
impl Marker for Empty {}
#[repr(align(16384))]
struct Backing(Empty);
static DATA: Backing = Backing(Empty);
const LAYOUT: (usize, usize) = unsafe {
    let object: &dyn Marker = &DATA.0;
    let pointer = (object as *const dyn Marker).wrapping_byte_sub(8192);
    let reference = &*pointer;
    (std::mem::size_of_val(reference), std::mem::align_of_val(reference))
};
fn main() { assert_eq!(LAYOUT, (0, 8192)); }
