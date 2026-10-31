macro_rules! excluded_type { () => { compile_error!("excluded enum tuple field expanded") }; }
enum Choice { Value(#[cfg_attr(all(), cfg(any()))] excluded_type!(), u64) }
fn main() { let Choice::Value(value) = Choice::Value(37); assert_eq!(value, 37); }
