// ecdsa matches a digest's `ObjectIdentifier` against constants:
// `match digest_oid { SHA224_OID => .., _ => None }`, and an
// `ObjectIdentifier` holds a `[u8; 39]`. rustc turns a constant pattern
// into a pattern per field and per array element, and a wildcard adds no
// test. Our decision tree wants every arm's rules in the same columns, but
// a `_` over an array of 32 or more elements is one rule, not one per
// element, and the lowering asserted on the mismatch.
#[derive(PartialEq, Eq, Clone, Copy)]
pub struct Oid {
    length: u8,
    bytes: [u8; 39],
}

const fn oid(a: u8) -> Oid {
    let mut bytes = [0; 39];
    bytes[0] = a;
    Oid { length: 1, bytes }
}

const A: Oid = oid(1);
const B: Oid = oid(2);

const fn by_constant(o: Oid) -> Option<u8> {
    match o {
        A => Some(1),
        B => Some(2),
        _ => None,
    }
}

fn by_elements(o: [u8; 39]) -> Option<u8> {
    match o {
        [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] => Some(1),
        _ => None,
    }
}

fn main() {
    assert_eq!(by_constant(A), Some(1));
    assert_eq!(by_constant(B), Some(2));
    assert_eq!(by_constant(oid(5)), None);
    assert_eq!(by_elements(A.bytes), Some(1));
    assert_eq!(by_elements(B.bytes), None);
}
