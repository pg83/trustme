#![feature(trait_alias)]
trait Provider { type Item; }
trait Left<T> = Provider<Item = T> where T: Clone;
trait Right<T> = Provider<Item = T> where T: Clone;
trait Diamond<T> = Left<T> + Right<T>;
struct Text;
impl Provider for Text { type Item = String; }
fn duplicate<T, P: Diamond<T>>(value: &P::Item) -> (T, T) { (value.clone(), value.clone()) }
fn main() {
    let value = String::from("diamond");
    assert_eq!(duplicate::<String, Text>(&value), (value.clone(), value));
}
