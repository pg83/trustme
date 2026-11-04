//@ compile-variant: -O
macro_rules! guarded_bool {
    ($name:ident, $pattern:expr) => {
        fn $name(value: bool, enabled: bool) -> u8 {
            match value { $pattern if enabled => 17, _ => 23 }
        }
    };
}

guarded_bool!(select_true, true);
guarded_bool!(select_false, false);

fn main() {
    for enabled in [false, true] {
        for value in [false, true] {
            assert_eq!(select_true(value, enabled), if value && enabled { 17 } else { 23 });
            assert_eq!(select_false(value, enabled), if !value && enabled { 17 } else { 23 });
        }
    }
}
