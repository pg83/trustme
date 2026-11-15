#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn extent<T>(_: &T) -> [u8; std::mem::size_of::<T>()] {
    [0; std::mem::size_of::<T>()]
}

fn main() {
    let select = |item: Result<u8, u8>| match item {
        Ok(value) | Err(value) => value,
    };
    let bytes = extent(&select);
    assert_eq!(bytes.len(), 0);
    assert_eq!(select(Ok(17)), 17);
    assert_eq!(select(Err(23)), 23);
}
