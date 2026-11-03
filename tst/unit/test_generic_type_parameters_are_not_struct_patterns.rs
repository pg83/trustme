//@ compile-fail: Couldn't find pattern type 'Shape'
fn inspect<Shape>(value: Shape) {
    let Shape {} = value;
}
fn main() { inspect(701u16); }
