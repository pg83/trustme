use std::mem;

#[repr(C)]
enum Value {
    A(u32),
    B { x: u8, y: i16, z: u8 },
    C,
    D(u32),
    E(u64),
}

#[repr(C)]
enum Tag {
    A,
    B,
    C,
    D,
    E,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct Fields {
    x: u8,
    y: i16,
    z: u8,
}

#[repr(C)]
union Payload {
    b: Fields,
    e: u64,
}

#[repr(C)]
struct Repr {
    tag: Tag,
    payload: Payload,
}

fn main() {
    let repr = Repr {
        tag: Tag::B,
        payload: Payload {
            b: Fields { x: 206, y: 1145, z: 78 },
        },
    };
    let value: Value = unsafe { mem::transmute(repr) };
    match value {
        Value::B { x, y, z } => assert_eq!((x, y, z), (206, 1145, 78)),
        _ => panic!(),
    }
}
