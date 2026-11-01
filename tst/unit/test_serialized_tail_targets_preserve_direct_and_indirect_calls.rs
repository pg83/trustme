//@ aux-build: tail_targets_both_kinds.rs

fn main() {
    assert_eq!(tail_targets_both_kinds::direct(true, 29u16), 29);
    assert_eq!(tail_targets_both_kinds::direct(false, (31u64, true)), (31, true));
    assert_eq!(tail_targets_both_kinds::indirect::<3>(true, 20), 23);
    assert_eq!(tail_targets_both_kinds::indirect::<3>(false, 20), 17);
    assert_eq!(tail_targets_both_kinds::indirect::<7>(true, 40), 47);
    assert_eq!(tail_targets_both_kinds::indirect::<7>(false, 40), 33);
}
