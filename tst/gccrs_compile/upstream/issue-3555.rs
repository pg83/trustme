
#[derive(PartialEq, Eq)]
struct SomeStruct(u8);

const STRUCT_CONST_1: SomeStruct = SomeStruct(1);

fn main() {
    match SomeStruct(2) {
        STRUCT_CONST_1 => {}
        _ => {}
    }
}
