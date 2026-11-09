//@ aux-build: constructor_binding_shapes.rs
//@ compile-fail: error
extern crate constructor_binding_shapes;

fn main() {
    let _constructor = constructor_binding_shapes::Choice::Named;
}
