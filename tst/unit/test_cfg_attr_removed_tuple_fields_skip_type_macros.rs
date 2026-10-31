macro_rules! excluded_type { () => { compile_error!("excluded tuple field expanded") }; }
struct Pair(#[cfg_attr(all(), cfg(any()))] excluded_type!(), u32);
fn main() { let Pair(value) = Pair(29); assert_eq!(value, 29); }
