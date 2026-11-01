//@ compile-variant: -O

fn triple(value: u16) -> u16 { value * 3 }
fn plus_five(value: u16) -> u16 { value + 5 }
struct Callbacks { entries: [(u16, fn(u16) -> u16); 2] }
const CALLBACKS: Callbacks = Callbacks { entries: [(7, triple), (11, plus_five)] };
#[inline(never)]
fn apply(value: Callbacks, index: usize) -> u16 {
    let (argument, function) = value.entries[index];
    function(argument)
}
fn main() {
    assert_eq!(apply(CALLBACKS, 0), 21);
    assert_eq!(apply(CALLBACKS, 1), 16);
    assert_eq!(apply(Callbacks { entries: [(13, plus_five), (17, triple)] }, 0), 18);
    assert_eq!(apply(Callbacks { entries: [(13, plus_five), (17, triple)] }, 1), 51);
}
