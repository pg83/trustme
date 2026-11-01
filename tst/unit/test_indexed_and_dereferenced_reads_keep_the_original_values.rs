#[inline(never)]
fn values(index: usize) -> (u16, u16, u16) {
    let mut array = [11, 17, 23];
    let indexed = array[index];
    array[index] = 31;
    let mut boxed = Box::new(37u16);
    let dereferenced = *boxed;
    *boxed = 41;
    (indexed, dereferenced, array[index] + *boxed)
}

fn main() {
    assert_eq!(values(0), (11, 37, 72));
    assert_eq!(values(2), (23, 37, 72));
}
