#![feature(rustc_attrs)]

#[rustc_std_internal_symbol]
fn coverage_internal_adjust(value: u16) -> u16 {
    value + 9
}

extern "Rust" {
    #[link_name = "coverage_internal_adjust"]
    fn linked_adjust(value: u16) -> u16;
}

fn main() {
    assert_eq!(coverage_internal_adjust(7), 16);
    assert_eq!(unsafe { linked_adjust(15) }, 24);
}
