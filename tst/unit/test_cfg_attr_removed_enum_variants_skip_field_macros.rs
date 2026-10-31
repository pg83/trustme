macro_rules! excluded_type { () => { compile_error!("excluded variant field expanded") }; }
enum Choice {
    #[cfg_attr(all(), cfg_attr(all(), cfg(any())))]
    Removed(excluded_type!()),
    Kept(u16),
}
fn main() { let Choice::Kept(value) = Choice::Kept(43); assert_eq!(value, 43); }
