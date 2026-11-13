//@ compile-only
#![allow(unreachable_code, unused_variables)]

struct Element;

impl Drop for Element {
    fn drop(&mut self) {}
}

fn main() {
    return;
    {
        let array: [Element; 32];
        let [first, ..] = array;
    }
}
