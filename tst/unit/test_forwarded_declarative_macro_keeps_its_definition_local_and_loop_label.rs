#![feature(decl_macro)]

macro_rules! forward {
    ($item:item) => {
        $item
    };
}

fn main() {
    let value = 'outer: loop {
        let captured = 43u16;
        forward! {
            macro leave() {
                break 'outer captured;
            }
        }
        let captured = 99u16;
        assert_eq!(captured, 99);
        leave!();
    };
    assert_eq!(value, 43);
}
