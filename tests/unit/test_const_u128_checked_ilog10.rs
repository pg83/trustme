const LOG: Option<u32> = 256u128.checked_ilog10();

fn main() {
    assert_eq!(std::mem::size_of::<Option<std::num::NonZeroU128>>(), 16);
    assert_eq!(LOG, Some(2));
}
