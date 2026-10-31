trait Family { type Item; }
impl Family for u32 { type Item = u32; }
fn identity<T>(value: T) -> T { value as T }
fn projected<T: Family<Item = T>>(value: T::Item) -> T { value as T }
fn main() {
    assert_eq!(identity(37u32), 37);
    assert_eq!(identity(String::from("wire")), "wire");
    assert_eq!(projected::<u32>(41), 41);
}
