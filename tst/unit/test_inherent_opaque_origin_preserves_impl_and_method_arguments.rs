struct Source<T>(T);
impl<T> Source<T> {
    fn pair<U>(self, other: U) -> impl FnOnce() -> (T, U) {
        move || (self.0, other)
    }
}
fn main() {
    let make = Source(743u16).pair(751u32);
    let (first, second) = make();
    assert_eq!(first, 743);
    assert_eq!(second, 751);
    let make = Source("spruce").pair([2u8, 3, 5]);
    assert_eq!(make(), ("spruce", [2, 3, 5]));
}
