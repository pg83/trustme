macro_rules! define_generated_macro {
    () => {
        macro_rules! generated {
            (marker $value:expr) => {
                $value + 1
            };
        }
    };
}

define_generated_macro!();

fn main() {
    assert_eq!(generated!(marker 4), 5);
}
