const PAIR: (u8, u32) = (7, 0x1234_5678);
const PAIR_FIELD: &u32 = &PAIR.1;

const OPTIONAL: Option<u32> = Some(0x8765_4321);
const OPTIONAL_PAYLOAD: Option<&u32> = OPTIONAL.as_ref();

const RESULT: Result<u32, u16> = Ok(0x0bad_f00d);
const RESULT_PAYLOAD: Result<&u32, &u16> = RESULT.as_ref();

fn main() {
    assert_eq!(*PAIR_FIELD, 0x1234_5678);
    assert_eq!(*OPTIONAL_PAYLOAD.unwrap(), 0x8765_4321);
    assert_eq!(*RESULT_PAYLOAD.unwrap(), 0x0bad_f00d);
}
