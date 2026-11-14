//@ compile-flags: -O

#[inline(never)]
fn copy_fields(value: ((u32, u32), u32)) -> (u32, u32) {
    let fields = value.0;
    let first = fields.0;
    let second = fields.1;
    (first, second + value.1)
}

fn main() {
    assert_eq!(copy_fields(((19, 23), 19)), (19, 42));
}
