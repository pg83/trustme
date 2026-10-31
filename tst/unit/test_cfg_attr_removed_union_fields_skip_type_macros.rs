macro_rules! excluded_type { () => { compile_error!("excluded union field expanded") }; }
union Bits {
    #[cfg_attr(all(), cfg(any()))]
    removed: excluded_type!(),
    kept: u32,
}
fn main() { let value = Bits { kept: 41 }; assert_eq!(unsafe { value.kept }, 41); }
