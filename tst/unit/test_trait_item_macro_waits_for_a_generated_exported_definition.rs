//@ compile-variant: -O
#![allow(macro_expanded_macro_exports_accessed_by_absolute_paths)]
macro_rules! install {
    () => {
        #[macro_export]
        macro_rules! deferred_method { () => { fn number() -> u16 { 617 } }; }
    };
}
trait Reading { crate::deferred_method!(); }
install!();
struct Gauge;
impl Reading for Gauge {}
fn main() { assert_eq!(Gauge::number(), 617); }
