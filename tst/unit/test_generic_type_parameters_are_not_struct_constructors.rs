//@ compile-fail: Couldn't find pattern type 'Shape'
fn construct<Shape>() {
    let _ = Shape { value: 709u16 };
}
fn main() { construct::<u16>(); }
