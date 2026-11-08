//@ compile-flags: -Zdump-ast -Zdump-hir
macro_rules! walk {
    ($values:expr) => {{
        macro_rules! step { ($value:expr) => { $value + 1 }; }
        let mut total = 0;
        'items: for value in $values {
            for offset in [0, 1] {
                if offset == 1 { continue 'items; }
                total += step!(value);
            }
        }
        total
    }};
}

const WIDTH: usize = {
    macro_rules! width { () => { 3 }; }
    width!()
};

fn main() {
    let run = |values: [u16; 3]| walk!(values);
    assert_eq!(run([3, 5, 7]), 18);
    assert_eq!(walk!([1u16; WIDTH]), 6);
}
