// Regression: a tuple-struct literal reached type checking through `Self`,
// so AST lowering could not turn it into the ordinary tuple constructor.

struct Gray<T>(T);

macro_rules! inherent_impl {
    ($name:ident, [$($field:tt $value:ident),*]) => {
        impl<T: Copy> $name<T> {
            const fn new($($value: T),*) -> Self {
                Self { $($field: $value),* }
            }
        }
    };
}

inherent_impl!(Gray, [0 value]);

fn main() {
    assert_eq!(Gray::new(42).0, 42);
}
