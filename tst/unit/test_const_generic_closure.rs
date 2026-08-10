// A `[T; N]` whose N is a parent const generic, used inside a closure, lost its
// binding when the closure was lifted (MIR inliner saw empty value params).
fn f<const N: usize>() -> Vec<[u8; N]> {
    (0..4u16).map(|i| { let mut a = [0u8; N]; a[0] = i as u8; a }).collect()
}
fn main() {
    let v = f::<3>();
    assert_eq!(v.len(), 4);
    assert_eq!(v[2][0], 2);
    assert_eq!(v[0].len(), 3);
}
