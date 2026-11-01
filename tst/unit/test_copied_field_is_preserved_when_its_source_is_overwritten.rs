#[inline(never)]
fn replace(value: &mut (u16, u16)) { value.0 = 31; value.1 = 37; }

#[inline(never)]
fn read(change: bool) -> (u16, u16, u16) {
    let mut source = (11, 17);
    let first = source.0;
    let second = source.1;
    if change { replace(&mut source); }
    (first, second, source.0 + source.1)
}

fn main() {
    assert_eq!(read(false), (11, 17, 28));
    assert_eq!(read(true), (11, 17, 68));
}
