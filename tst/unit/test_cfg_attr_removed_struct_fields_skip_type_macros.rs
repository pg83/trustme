macro_rules! excluded_type { () => { compile_error!("excluded struct field expanded") }; }
struct Record {
    #[cfg_attr(all(), cfg(any()))]
    removed: excluded_type!(),
    kept: u16,
}
fn main() { assert_eq!(Record { kept: 23 }.kept, 23); }
