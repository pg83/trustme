#[inline(never)]
fn projected(input: ((u32, u32), u32)) -> u32 {
    let source = input;
    let alias = source;
    let inner = alias.0;
    let leaf = inner.0;
    leaf ^ source.1
}

fn main() {
    assert_eq!(projected(((29, 7), 11)), 22);
    assert_eq!(projected(((0xffff_0000, 3), 0x00ff_00ff)), 0xff00_00ff);
}
