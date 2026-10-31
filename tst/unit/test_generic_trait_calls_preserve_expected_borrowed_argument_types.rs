trait Read {
    fn read(value: &str) -> usize;
}
struct Reader;
impl Read for Reader { fn read(value: &str) -> usize { value.len() } }
fn invoke<T: Read>(value: &String) -> usize { T::read(value) }
fn main() {
    let value = String::from("rust");
    assert_eq!(invoke::<Reader>(&value), 4);
    assert_eq!(value, "rust");
}
