//@ compile-fail: constant reference may be null
static EMPTY: () = ();
const VALUE: &() = unsafe { &*((&EMPTY as *const ()).cast::<u8>().wrapping_sub(1).cast::<()>()) };
fn main() { let _ = std::hint::black_box(VALUE); }
