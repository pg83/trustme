//@ proc-macro-aux-build: fn_pointer_token_order.rs
// A function pointer type handed to a proc macro reads `unsafe extern "C"
// fn(..)` as written (rustc's print_ty_fn: safety, then the ABI, then `fn`),
// variadic `...` included.
use fn_pointer_token_order::FieldOrder;

#[derive(FieldOrder)]
#[allow(dead_code)]
struct Functions {
    printf: unsafe extern "C" fn(*const u8, ...) -> i32,
}

fn main() {
    assert!(FIELD_ORDER);
}
