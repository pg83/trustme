#![feature(decl_macro)]

fn main() {
    let value = 'outer: loop {
        macro leave() {
            break 'outer 41u16;
        }
        leave!();
    };
    assert_eq!(value, 41);
}
