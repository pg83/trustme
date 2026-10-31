#![feature(trait_alias)]
macro_rules! item { ($item:item) => { $item }; }
item! { trait Measure = for<'a> Fn(&'a str) -> usize; }
fn apply<F: Measure>(f: F) -> (usize,usize) { (f("oak"), f("willow")) }
fn length(text: &str) -> usize { text.len() }
fn main() { assert_eq!(apply(length),(3,6)); }
