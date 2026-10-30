const fn width() -> usize { 4 }
union Storage<T: Copy = [u8; width()]> {
    bytes: T,
    word: u32,
}
impl Storage {
    fn make() -> Self { Self { bytes: [3, 5, 7, 11] } }
}
fn main() {
    let value = Storage::make();
    assert_eq!(unsafe { value.bytes }, [3, 5, 7, 11]);
}
