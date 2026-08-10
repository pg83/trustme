const VALUE: &i32 = &1;
const SLICE: &[i32] = core::slice::from_ref(VALUE);

fn main() {
    assert!(core::ptr::eq(VALUE, &SLICE[0]));
}
