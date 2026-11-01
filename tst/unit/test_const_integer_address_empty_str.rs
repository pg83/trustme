const EMPTY: &str = unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(1usize as *const u8,0)) };
fn main() { let value=std::hint::black_box(EMPTY);assert!(value.is_empty());assert_eq!(value.as_ptr() as usize,1); }
