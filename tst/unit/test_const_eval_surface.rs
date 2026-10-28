//@ crate-type: lib
#![allow(dead_code)]

#[derive(Copy, Clone)]
struct StructValue {
    a: i32,
    b: [u8; 2],
}

enum EnumValue {
    A,
    B(i32),
}

const STRUCT: StructValue = StructValue { a: 3, b: [1, 2] };
const ENUM: EnumValue = EnumValue::B(4);
const FLOAT: f64 = 1.5;
const NEG_FLOAT: f32 = -2.25;
const SHIFT: u64 = 1 << 63;
const NEG_SHIFT: i64 = -1 >> 63;
const ARRAY: [StructValue; 2] = [STRUCT, StructValue { a: 5, b: [6, 7] }];
const REFERENCE: &StructValue = &STRUCT;
const TUPLE: (i32, f64, bool) = (1, FLOAT, true);

pub fn constants() -> i32 {
    let enum_value = match ENUM {
        EnumValue::A => 0,
        EnumValue::B(value) => value,
    };
    STRUCT.a + ARRAY[1].a + enum_value + FLOAT as i32 + NEG_FLOAT as i32 + SHIFT as i32 + NEG_SHIFT as i32 + REFERENCE.a + TUPLE.0 + TUPLE.2 as i32
}
