static BYTES: [u8; 37] = [41; 37];
static REFERENCE: &[u8; 37] = &BYTES;

fn call<T: 'static, F: FnOnce() -> &'static T>(function: F) -> &'static T {
    function()
}

fn main() {
    let carried = call(|| &REFERENCE);
    let actual = carried as *const _ as usize;
    let expected = &REFERENCE as *const _ as usize;
    assert_eq!(actual, expected);
}
