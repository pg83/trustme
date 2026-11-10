#![feature(decl_macro)]

macro_rules! forward {
    ($statement:stmt) => {
        $statement
    };
}

fn main() {
    let value = 'outer: loop {
        let captured = 53u16;
        forward! {
            macro leave() {
                break 'outer captured;
            }
        }
        let captured = 99u16;
        assert_eq!(captured, 99);
        leave!();
    };
    assert_eq!(value, 53);
}
