// compile-flags: -Zvalidate-mir
#![feature(deref_patterns)]
#![allow(incomplete_features)]

enum Wrapped {
    Text(String),
}

fn main() {
    assert!(matches!(*"ok", "ok"));

    let mut value = String::from("ok");
    let deref!(ref mut bytes) = value;
    bytes.make_ascii_uppercase();
    assert_eq!(value, "OK");

    let Wrapped::Text(deref!("nested")) = Wrapped::Text(String::from("nested")) else {
        panic!();
    };

    let (deref!("left") | _, deref!("right")) =
        (String::from("left"), String::from("right"))
    else {
        panic!();
    };

    let numbers = vec![1, 2, 3];
    let [.., last] = numbers else { unreachable!() };
    assert_eq!(last, 3);
    assert_eq!(numbers, [1, 2, 3]);

    for (byte, expected) in [(b"0", 0), (b"1", 1), (b"2", 2)] {
        let direct = match *byte {
            b"0" => 0,
            b"1" => 1,
            _ => 2,
        };
        assert_eq!(direct, expected);

        let slice = match *(byte as &[u8]) {
            b"0" => 0,
            b"1" => 1,
            _ => 2,
        };
        assert_eq!(slice, expected);

        let decoded = match Box::new(*byte) {
            deref!(b"0") => 0,
            b"1" => 1,
            _ => 2,
        };
        assert_eq!(decoded, expected);
    }
}
