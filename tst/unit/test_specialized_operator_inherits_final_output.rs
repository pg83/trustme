#![feature(specialization)]
#![allow(incomplete_features)]
use std::ops::Add;
struct Count;
impl<T> Add<T> for Count {
    type Output = usize;
    default fn add(self, _: T) -> usize { 1 }
}
impl Add<&str> for Count { fn add(self, rhs: &str) -> usize { rhs.len() + 10 } }
impl Add<&String> for Count { fn add(self, rhs: &String) -> usize { rhs.len() + 20 } }
fn main() {
    let text = String::from("abc");
    assert_eq!(Count + &text, 23);
    assert_eq!(Count + text.as_str(), 13);
    assert_eq!(Count + (|| &text)(), 23);
    assert_eq!(Count + (|| text.as_str())(), 13);
}
