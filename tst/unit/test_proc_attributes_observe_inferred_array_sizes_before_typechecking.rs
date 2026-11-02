//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-variant: -O
#[inspect_type_expansion_order::replace_inferred_arrays(STRUCT_MARKERS)]
struct Erased { first: [u8; _], second: Option<[u16; _]> }
#[inspect_type_expansion_order::replace_inferred_arrays(FUNCTION_MARKERS)]
fn erased(_: [u32; _]) -> [u8; _] { loop {} }
fn main() {
    assert_eq!(STRUCT_MARKERS, 2);
    assert_eq!(FUNCTION_MARKERS, 2);
}
