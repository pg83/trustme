#![feature(const_raw_ptr_comparison)]

const COMPARISON: Option<bool> = {
    let byte = 41u8;
    (&byte as *const u8).guaranteed_eq(1usize as *const u8)
};

fn main() {
    assert_eq!(COMPARISON, None);
}
