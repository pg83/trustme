macro_rules! excluded_value { () => { compile_error!("excluded discriminant expanded") }; }
#[repr(u8)]
enum Choice {
    #[cfg_attr(all(), cfg(any()))]
    Removed = excluded_value!(),
    Kept = 47,
}
fn main() { assert_eq!(Choice::Kept as u8, 47); }
