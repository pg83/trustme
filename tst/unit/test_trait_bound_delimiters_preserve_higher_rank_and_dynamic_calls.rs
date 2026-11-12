//@ edition: 2015
#![allow(bare_trait_objects)]

trait Project {
    type Value;
}

impl Project for u16 {
    type Value = u16;
}

fn keep<T: Project<Value: Copy +> +>(value: T) -> T {
    value
}

trait Score {
    fn score(&self) -> u16;
}

impl Score for u16 {
    fn score(&self) -> u16 {
        *self + 2
    }
}

fn measure(text: &str) -> usize {
    text.len()
}

fn main() {
    let value = keep(947u16);
    let object: &dyn (Score) = &value;
    assert_eq!(object.score(), 949);
    let callback: &for<'a> Fn(&'a str) -> usize = &measure;
    assert_eq!(callback("bounds"), 6);
}
