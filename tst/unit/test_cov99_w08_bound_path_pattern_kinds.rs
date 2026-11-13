//@ edition: 2021

struct Marker;
struct Pair(u8);
struct Named { value: u8 }
union Bits { byte: u8 }
enum Branch { Unit, Tuple(u8), Named { value: u8 } }

fn main() {
    let Marker = Marker;
    let Pair(pair) = Pair(11);
    let Named { value: named } = Named { value: 13 };
    assert_eq!(pair, 11);
    assert_eq!(named, 13);

    let unit = match Branch::Unit {
        Branch::Unit => 17,
        _ => 0,
    };
    let tuple = match Branch::Tuple(19) {
        Branch::Tuple(value) => value,
        _ => 0,
    };
    let named_variant = match (Branch::Named { value: 23 }) {
        Branch::Named { value } => value,
        _ => 0,
    };
    assert_eq!(unit, 17);
    assert_eq!(tuple, 19);
    assert_eq!(named_variant, 23);

    unsafe {
        let Bits { byte } = Bits { byte: 29 };
        assert_eq!(byte, 29);
    }
}
