macro_rules! excluded_type { () => { compile_error!("excluded enum named field expanded") }; }
enum Choice {
    Value {
        #[cfg_attr(all(), cfg(any()))]
        removed: excluded_type!(),
        kept: u16,
    },
}
fn main() { let Choice::Value { kept } = Choice::Value { kept: 31 }; assert_eq!(kept, 31); }
