#![feature(impl_trait_in_bindings)]

struct Value {
    field: u32,
}

fn capture(value: &mut &mut Value) {
    let _: impl AsyncFn() = async move || {
        let _ = &value.field;
    };
}

fn main() {}
