macro_rules! define_value {
    ($name:ident, $value:expr) => {
        macro_rules! $name {
            () => {
                $value
            };
        }
    };
}

define_value!(generated_value, 37);

fn main() {
    assert_eq!(generated_value!(), 37);
}
