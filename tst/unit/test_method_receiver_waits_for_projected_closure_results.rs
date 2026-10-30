struct Parcel<T>(T);
impl<T> Parcel<T> {
    fn project<U, F: FnOnce(T) -> U>(self, f: F) -> Parcel<U> { Parcel(f(self.0)) }
    fn or(self, _: T) -> T { self.0 }
}
fn main() {
    let bytes = Parcel("letters").project(|text| text.as_bytes()).or(b"");
    assert_eq!(bytes, b"letters");
    let words = Parcel([2u8, 7, 9]).project(|array| array.to_vec()).or(Vec::new());
    assert_eq!(words, [2, 7, 9]);
}
