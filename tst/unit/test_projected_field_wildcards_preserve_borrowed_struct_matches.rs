trait Family { type Item; }
impl Family for u8 { type Item = u32; }
struct Wrapper<T: Family> { value: T::Item }
fn choose<T: Family>(choice: bool, value: &Wrapper<T>) -> u8 {
    match (choice, value) { (true | false, Wrapper { value: _ }) => 17 }
}
fn main() {
    let value = Wrapper::<u8> { value: 37 };
    assert_eq!((choose(false, &value), choose(true, &value), value.value), (17, 17, 37));
}
