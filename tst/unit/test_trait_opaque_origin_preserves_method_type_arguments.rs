trait Pair<T> {
    fn pair<U>(self, other: U) -> impl FnOnce() -> (T, U);
}
struct Source<T>(T);
impl<T> Pair<T> for Source<T> {
    fn pair<U>(self, other: U) -> impl FnOnce() -> (T, U) {
        move || (self.0, other)
    }
}
fn main() {
    let first = <Source<u16> as Pair<u16>>::pair(Source(757), 761u32);
    assert_eq!(first(), (757, 761));
    let second = Source([7u8, 11]).pair("fir");
    assert_eq!(second(), ([7, 11], "fir"));
}
