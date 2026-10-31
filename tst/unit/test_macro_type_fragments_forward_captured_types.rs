macro_rules! typed {
    ($ty:ty, $value:expr) => {{ let value: $ty = $value; value }};
}
macro_rules! forward {
    ($ty:ty, $value:expr) => { typed!($ty, $value) };
}
fn main() {
    assert_eq!(forward!(Option<[u8; 2]>, Some([3, 5])), Some([3, 5]));
    let value = 17u32;
    assert_eq!(*forward!(&u32, &value), 17);
    let callback = forward!(fn(u8) -> u8, |n| n + 2);
    assert_eq!(callback(7), 9);
}
