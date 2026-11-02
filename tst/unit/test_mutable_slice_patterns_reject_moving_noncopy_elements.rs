//@ compile-fail: move out
fn take(values: &mut [String]) {
    match *values { [first, ..] => drop(first), [] => () }
}
fn main() { take(&mut [String::from("a"), String::from("b")]); }
