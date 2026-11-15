//@ edition: 2021
//@ compile-fail: incompatible type for trait

trait HasSlice {
    const VALUE: [u8];
}

impl HasSlice for () {
    const VALUE: [u8; 2] = [11, 23];
}

fn main() {
    let bytes: &[u8] = &[11, 23];
    match *bytes {
        <() as HasSlice>::VALUE => (),
        _ => (),
    }
}
