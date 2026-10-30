#![feature(fn_delegation)]
#![allow(incomplete_features)]
trait Source: Sized + Copy {
 fn values(value: Self) -> impl Iterator<Item=Self> + Clone { [value, value].into_iter() }
 fn count(values: impl Iterator<Item=Self>) -> usize { values.count() }
}
impl Source for u16 {}
reuse Source::{values, count};
fn main() {
 let output=values(17u16);
 assert_eq!(output.clone().sum::<u16>(),34);
 assert_eq!(count(output),2);
}
