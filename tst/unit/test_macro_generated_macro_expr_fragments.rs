macro_rules! define_macro {
    ($name:ident, $value:expr) => {
        macro_rules! $name {
            () => {
                $value
            };
        }
    };
}

macro_rules! source {
    () => {
        11
    };
}

macro_rules! define_pair {
    ($name:ident, $captured:expr) => {
        let definition_site = 13;
        macro_rules! $name {
            () => {
                ($captured, definition_site)
            };
        }
    };
}

fn main() {
    let value = 7;
    define_macro!(from_value, value);
    define_macro!(from_macro, source!());

    let captured = 17;
    define_pair!(pair, captured);

    let value = 9;
    let captured = 19;
    let definition_site = 23;
    assert_eq!(from_value!(), 7);
    assert_eq!(from_macro!(), 11);
    assert_eq!(pair!(), (17, 13));
}
