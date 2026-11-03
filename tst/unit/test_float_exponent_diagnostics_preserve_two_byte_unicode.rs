//@ compile-fail: Non-numeric 'é' in float exponent
fn main() { let _value = 1eé; }
