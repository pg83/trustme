static EMPTY: u16 = 257;
const VALUE: () = unsafe { *((&EMPTY as *const u16).cast::<u8>().wrapping_sub(1).cast::<()>()) };
fn main() { assert_eq!(VALUE, ()); }
