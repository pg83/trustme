// A binding whose type is `!` keeps that type: every use coerces on its own,
// instead of the first use deciding what the binding is.
#![feature(never_type)]

struct Wrap(!);

fn from_result(x: Result<u32, !>) -> u32 {
    match x {
        Ok(v) => v,
        Err(y) => {
            let _a: i32 = y;
            let _b: String = y;
            y
        }
    }
}

fn from_field(x: Option<Wrap>) -> u32 {
    if let Some(Wrap(y)) = x {
        let _a: i32 = y;
        let _b: Vec<u8> = y;
        y
    } else {
        7
    }
}

fn main() {
    assert_eq!(from_result(Ok(123)), 123);
    assert_eq!(from_field(None), 7);
}
