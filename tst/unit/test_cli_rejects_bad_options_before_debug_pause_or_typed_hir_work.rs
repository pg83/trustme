//@ reject-flags: -Zpause-after-start --round267-invalid-option => Unrecognized option || Unrecognized option
//@ reject-flags: -Zunpretty=hir,typed --round267-invalid-option => Unrecognized option || Unrecognized option
//@ compile-variant: -O

fn rotate(values: &[u32]) -> u32 {
    values.iter().fold(0u32, |acc, &value| acc.rotate_left(3) ^ value)
}
fn main() {
    assert_eq!(rotate(&[5, 2]), 42);
    assert_eq!(rotate(&[]), 0);
}
