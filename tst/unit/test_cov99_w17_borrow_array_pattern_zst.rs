#[inline(never)]
fn middle(values: &[[u64; 0]; 3]) -> &[u64; 0] {
    let [_, middle, _] = values;
    middle
}

fn main() {
    let values = [[0u64; 0]; 3];
    let pointer = middle(&values) as *const [u64; 0];
    assert!(!pointer.is_null());
    assert_eq!(pointer as usize % core::mem::align_of::<u64>(), 0);
}
