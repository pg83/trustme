//@ aux-build: late_const_heap_exports.rs

fn main() {
    assert_eq!(late_const_heap_exports::final_read(), (29, 31));
    assert_eq!(late_const_heap_exports::read_alias(), (29, 31));
    assert_eq!(late_const_heap_exports::NUMBER_ALIAS, 5);
    assert_eq!(late_const_heap_exports::STATIC_ALIAS, 13);
    assert_eq!(late_const_heap_exports::root_alias(), 19);
}
