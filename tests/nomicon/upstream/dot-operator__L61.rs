// Extracted from src/dot-operator.md:61
#![allow(unused)]
fn main() {
    fn do_stuff<T: Clone>(value: &T) {
        let cloned = value.clone();
    }
}
