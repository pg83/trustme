//@ compile-fail: move out
fn take(values: Box<[String]>) {
    match *values { [first, ..] => drop(first), [] => () }
}
fn main() { take(vec![String::from("a"), String::from("b")].into_boxed_slice()); }
