macro_rules! scalar { () => { u32 }; }
macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }
macro_rules! forward { ($ty:ty) => { spelling!($ty) }; }
fn main() {
    assert!(forward!(scalar!()).contains("scalar"));
    assert!(forward!(unsafe extern "C" fn(fn(), *const u8) -> !).contains("extern"));
    assert!(forward!(for<'a> fn(&'a [u8]) -> &'a str).contains("for"));
    assert!(forward!(impl Clone + use<>).contains("use"));
    assert!(forward!(fn(fn()) -> ()).contains("fn"));
    assert_eq!(std::mem::size_of::<scalar!()>(), 4);
}
