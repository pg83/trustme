//@ compile-fail: error:

#[271]
fn marked() -> u16 {
    277
}

fn main() {
    let _ = marked();
}
