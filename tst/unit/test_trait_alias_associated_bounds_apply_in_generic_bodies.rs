#![feature(trait_alias)]
trait Provider { type Item; }
trait Clonable = Provider<Item: Clone>;
trait Repeated = Clonable;
struct Text;
impl Provider for Text { type Item = String; }
fn duplicate<T: Repeated>(value: &T::Item) -> T::Item { value.clone() }
fn main() {
    let value = String::from("echo");
    let copy = duplicate::<Text>(&value);
    assert_eq!(value, copy);
}
