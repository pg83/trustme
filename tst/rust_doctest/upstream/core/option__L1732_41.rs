// Extracted from library/core/src/option.rs:1732
#![allow(unused)]
fn main() {
    let mut x = None;

    {
        let y: &mut u32 = x.get_or_insert(5);
        assert_eq!(y, &5);

        *y = 7;
    }

    assert_eq!(x, Some(7));
}
