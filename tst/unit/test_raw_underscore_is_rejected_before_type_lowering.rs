//@ compile-fail: invalid raw identifier

fn main() {
    let _: r#_ = 17u16;
}
