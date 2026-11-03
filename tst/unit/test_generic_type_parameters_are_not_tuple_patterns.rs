//@ compile-fail: Couldn't find constant name 'Shape'
fn inspect<Shape>(value: Shape) {
    match value { Shape(_) => () }
}
fn main() { inspect(703u16); }
