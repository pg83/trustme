//@ compile-flags: -Cpanic=abort

macro_rules! outer {
    ($input:expr) => {{
        let value = $input;
        'done: loop {
            macro_rules! middle {
                () => {{
                    let nested = value + 1;
                    macro_rules! leaf {
                        () => {
                            break 'done nested;
                        };
                    }
                    leaf!();
                }};
            }
            middle!();
        }
    }};
}

fn main() {
    assert_eq!(outer!(40), 41);
    assert_eq!(outer!(80), 81);
}
