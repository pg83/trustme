#![feature(fn_delegation)]
fn increment(value: u32) -> u32 { value + 673 }
fn pair(value: u16, flag: bool) -> (u16, bool) { (value, flag) }
fn main() {
    reuse crate::increment as local;
    reuse crate::pair as local_pair;
    assert_eq!(local(4), 677);
    assert_eq!(local_pair(683, true), (683, true));
    {
        reuse crate::increment as inner;
        assert_eq!(inner(18), 691);
    }
}
