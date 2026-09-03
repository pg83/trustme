// An item declared inside a const-generic argument block is an ordinary
// nested item. It has to be registered under a resolvable path like any other
// item in an anonymous scope, or looking its type up later fails outright.

trait X<const N: i32> {
    fn value() -> i32 {
        N
    }
}

struct Marker;

impl X<1> for Marker {}

fn with_fn<T: X<{ fn helper() -> ! { loop {} } 1 }>>() -> i32 {
    T::value()
}

fn with_struct<T: X<{ struct Inner(u8); 1 }>>() -> i32 {
    T::value()
}

fn main() {
    assert_eq!(with_fn::<Marker>(), 1);
    assert_eq!(with_struct::<Marker>(), 1);
}
