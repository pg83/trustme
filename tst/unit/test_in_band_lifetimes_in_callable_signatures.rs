#![feature(in_band_lifetimes)]
fn borrow<T>(value: &'a T) -> &'a T { value }
fn apply<T>(callback: for<'b> fn(&'outer T, &'b u16) -> u16, value: &'outer T) -> u16 {
    callback(value, &7)
}
fn add(left: &u16, right: &u16) -> u16 { *left + *right }
struct View<'a>(&'a u16);
impl View<'a> { fn get(&self) -> &'a u16 { self.0 } }
fn main() {
    let number = 11;
    assert_eq!(*borrow(&number), 11);
    assert_eq!(apply(add, &number), 18);
    assert_eq!(*View(&number).get(), 11);
}
