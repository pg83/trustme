//@ proc-macro-aux-build: precise_capture_tokens.rs
use precise_capture_tokens::keep;
#[keep("implIterator<Item=&'aT>+Send+'a+use<'a,T,N>")]
fn source<'a, T: Sync, const N: usize>(values: &'a [T; N])
    -> impl Iterator<Item = &'a T> + Send + 'a + use<'a, T, N>
{
    values.iter()
}
#[keep("implfor<'r>Fn(&'ru8)->u8+Send+'static+use<>")]
fn handler() -> impl for<'r> Fn(&'r u8) -> u8 + Send + 'static + use<> { |value| *value + 3 }
#[keep("implcore::fmt::Debug+?Sized+use<>")]
fn opaque() -> &'static (impl core::fmt::Debug + ?Sized + use<>) { &7u8 }
fn main() {
    let array = [5u8, 7, 9];
    assert_eq!(source(&array).copied().collect::<Vec<_>>(), vec![5, 7, 9]);
    assert_eq!(handler()(&6), 9);
    assert_eq!(format!("{:?}", opaque()), "7");
}
