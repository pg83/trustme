const BYTES: &[u8;3] = unsafe { &*(b"larch!".as_ptr().add(1) as *const [u8;3]) };
fn main() { assert_eq!(std::hint::black_box(BYTES),b"arc"); }
