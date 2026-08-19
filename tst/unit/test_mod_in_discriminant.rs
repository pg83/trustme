// A variant's discriminant is an expression like any other: it may hold items,
// and those see the prelude the same way.
enum Weird {
    Variant = {
        mod tmp {
            pub struct Bar;

            pub const fn seven() -> isize {
                7
            }
        }

        let _ = tmp::Bar;
        tmp::seven()
    },
}

fn main() {
    assert_eq!(Weird::Variant as isize, 7);
}
