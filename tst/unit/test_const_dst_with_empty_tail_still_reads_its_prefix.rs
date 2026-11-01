#[repr(C)]
struct Wrapper<T: ?Sized> { header: u16, tail: T }
static VALUE: Wrapper<[u8; 0]> = Wrapper { header: 257, tail: [] };
const RESULT: (usize, u16) = unsafe {
    let data = (&VALUE as *const Wrapper<[u8; 0]>).cast::<u8>();
    let reference = &*(std::ptr::slice_from_raw_parts(data, 0) as *const Wrapper<[u8]>);
    (std::mem::size_of_val(reference), reference.header)
};
fn main() { assert_eq!(RESULT, (2, 257)); }
