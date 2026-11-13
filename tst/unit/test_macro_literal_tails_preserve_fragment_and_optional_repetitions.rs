macro_rules! values {
    ($($value:expr),+; done) => {
        0u16 $(+ $value)+
    };
}

macro_rules! optional {
    ($(enabled)? end) => {
        907u16
    };
}

macro_rules! groups {
    ($(left right)+ stop) => {
        911u16
    };
}

fn main() {
    assert_eq!(values!(19, 23; done), 42);
    assert_eq!(optional!(end), 907);
    assert_eq!(optional!(enabled end), 907);
    assert_eq!(groups!(left right left right stop), 911);
}
