// `type_name` prints a type the way it is written. A trait object went through
// the compiler's own type printer instead, which decorates paths with crate
// tags and wraps the bound list in parentheses: `dyn Send` came out as
// `dyn (+::"core-0_0_0"::marker::Send)`.
//
// Same shapes as the library tests std/type-name-unsized.rs and
// coretests/any.rs::dyn_type_name.
fn main() {
    assert_eq!(
        std::any::type_name::<dyn Send>(),
        "dyn core::marker::Send"
    );
    assert_eq!(
        std::any::type_name::<dyn Fn(i32, i32) -> i32>(),
        "dyn core::ops::function::Fn(i32, i32) -> i32"
    );
    // Unsized and reference types were already right.
    assert_eq!(std::any::type_name::<[u8]>(), "[u8]");
    assert_eq!(std::any::type_name::<&i32>(), "&i32");
}
