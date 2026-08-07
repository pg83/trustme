const HUGE: &[()] = &[(); usize::MAX];

fn main() {
    assert_eq!(HUGE.len(), usize::MAX);
    assert_eq!(core::mem::size_of_val(HUGE), 0);
}
