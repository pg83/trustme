macro_rules! return_type { ($ty:ty) => { $ty }; }
fn values() -> return_type!(impl Iterator<Item = u8> + use<>) {
    [2, 3, 5].into_iter()
}
fn callback() -> return_type!(impl for<'a> Fn(&'a str) -> &'a str + Copy + use<>) {
    |text| text
}
fn main() {
    assert_eq!(values().fold(0u8, |sum, value| sum + value), 10);
    let first = callback();
    let second = first;
    let text = String::from("captured");
    assert_eq!(first(&text), "captured");
    assert_eq!(second("static"), "static");
}
