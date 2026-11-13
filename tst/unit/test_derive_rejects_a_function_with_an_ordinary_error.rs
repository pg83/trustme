//@ compile-fail: error:0: derive may only be applied to structs, enums and unions

#[derive(Clone)]
fn annotated() -> u16 {
    809
}

fn main() {
    let _ = annotated();
}
