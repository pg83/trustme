//@ compile-variant: -O
macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }
fn compact(text: &str) -> String {
    text.chars().filter(|ch| !ch.is_whitespace()).collect()
}
fn main() {
    assert_eq!(compact(spelling!(impl Clone + Send + 'static)),
        "implClone+Send+'static");
    assert_eq!(compact(spelling!(impl core::fmt::Debug + 'scope + use<'scope>)),
        "implcore::fmt::Debug+'scope+use<'scope>");
}
