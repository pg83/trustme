//@ crate-type: lib

pub async fn guard_returns(gate: bool) -> u32 {
    let value = 7_u32;
    let borrowed = &value;
    if gate && { return *borrowed; } {
        11
    } else {
        *borrowed + 1
    }
}
