#![feature(never_patterns)]

enum Empty {}

fn main() {
    assert!({
        let (only,) = (1091u16,);
        let [first, second, middle @ .., last] = [1u16, 2, 3, 4, 5];
        let result: Result<u16, Empty> =
            Ok(only + first + second + middle[0] + middle[1] + last);
        match result {
            Ok(value) => value == 1106,
            Err(!),
        }
    });
}
