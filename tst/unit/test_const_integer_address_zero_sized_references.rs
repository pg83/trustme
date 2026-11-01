const UNIT: &() = unsafe { &*(1usize as *const ()) };
const ARRAY: &[u16;0] = unsafe { &*(2usize as *const [u16;0]) };
fn main() { assert_eq!(*std::hint::black_box(UNIT),());assert_eq!(std::hint::black_box(ARRAY).len(),0);assert_eq!(UNIT as *const () as usize,1);assert_eq!(ARRAY as *const _ as usize,2); }
