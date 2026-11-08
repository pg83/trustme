//@ compile-fail: #[repr(...)] with a type that does not name a representation
macro_rules! repr_from_type { ($ty:ty) => { #[repr($ty)] struct Value(u8); }; }
repr_from_type!((u8, u8));
fn main() {}
