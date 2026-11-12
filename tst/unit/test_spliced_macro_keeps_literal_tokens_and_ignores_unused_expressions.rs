macro_rules! build_literal {
    (($($prefix:tt)*) $literal:literal, $unused:expr) => {
        $($prefix)* {
            () => { $literal };
        }
    };
}

build_literal!((macro_rules! message) "preserved literal", missing_name());

fn main() {
    assert_eq!(message!(), "preserved literal");
}
