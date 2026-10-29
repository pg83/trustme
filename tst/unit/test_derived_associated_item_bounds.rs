//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::HelperForward;
#[derive(HelperForward)]
struct Holder<T: Iterator<Item: Copy + std::fmt::Debug> = std::array::IntoIter<u8, 2>>(T);
fn main() {
    let mut holder = Holder([3u8, 7].into_iter());
    assert_eq!(holder.0.next(), Some(3));
    assert_eq!(Holder::helper_forward_seen(), 1);
}
