macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }
fn compact(text: &str) -> String { text.chars().filter(|c| !c.is_whitespace()).collect() }
fn main() {
    assert_eq!(compact(spelling!(impl Iterator<Item = &'a T> + Send + 'a + use<'a, T, N>)),
        "implIterator<Item=&'aT>+Send+'a+use<'a,T,N>");
    assert_eq!(compact(spelling!(impl for<'r> Fn(&'r u8) -> u8 + Send + 'static + use<>)),
        "implfor<'r>Fn(&'ru8)->u8+Send+'static+use<>");
    assert_eq!(compact(spelling!(impl core::fmt::Debug + ?Sized + use<>)),
        "implcore::fmt::Debug+?Sized+use<>");
}
