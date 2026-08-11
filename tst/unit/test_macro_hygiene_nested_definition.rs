fn main() {
    let x = 0;
    macro_rules! define_macros {
        ($before:ident, $after:ident, $callsite_x:ident) => {
            macro_rules! $before {
                () => {
                    ($callsite_x, x)
                };
            }
            let x = 1;
            macro_rules! $after {
                () => {
                    ($callsite_x, x)
                };
            }
        };
    }

    let x = 2;
    define_macros!(before, after, x);

    let x = 3;
    assert_eq!(before!(), (2, 0));
    assert_eq!(after!(), (2, 1));
}
