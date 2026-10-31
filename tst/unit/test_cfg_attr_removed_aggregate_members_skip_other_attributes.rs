macro_rules! excluded_type { () => { compile_error!("removed member expanded") }; }
struct Named {
    #[cfg_attr(all(), cfg(any()))]
    #[derive(Clone)]
    removed: excluded_type!(),
    kept: u16,
}
struct Tuple(#[cfg_attr(all(), cfg(any()))] #[derive(Clone)] excluded_type!(), u32);
enum Choice {
    #[cfg_attr(all(), cfg(any()))]
    #[derive(Clone)]
    Removed(excluded_type!()),
    Kept {
        #[cfg_attr(all(), cfg(any()))]
        #[derive(Clone)]
        removed: excluded_type!(),
        value: u16,
    },
}
union Bits {
    #[cfg_attr(all(), cfg(any()))]
    #[derive(Clone)]
    removed: excluded_type!(),
    value: u16,
}
fn main() {
    assert_eq!(Named { kept: 23 }.kept, 23);
    let Tuple(value) = Tuple(29);
    assert_eq!(value, 29);
    let Choice::Kept { value } = Choice::Kept { value: 31 };
    assert_eq!(value, 31);
    assert_eq!(unsafe { Bits { value: 37 }.value }, 37);
}
