//@ compile-fail: pattern does not mention all fields
enum Choice { Named { first: u8, second: u16 }, Empty }
fn main() {
    match (Choice::Named { first: 19, second: 23 }) {
        Choice::Named { first } => { let _ = first; },
        Choice::Empty => {},
    }
}
