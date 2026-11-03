macro_rules! seventeen { () => { 17 }; }
macro_rules! repeat {
    ($value:expr; $pattern:pat => $body:expr) => {{
        let first = match $value { $pattern => $body, _ => 0 };
        let second = match $value { $pattern => $body, _ => 0 };
        (first, second)
    }};
}

fn main() {
    assert_eq!(repeat!(17; selected @ seventeen!() => selected + 1), (18, 18));
    assert_eq!(repeat!(19; selected @ seventeen!() => selected + 1), (0, 0));
}
