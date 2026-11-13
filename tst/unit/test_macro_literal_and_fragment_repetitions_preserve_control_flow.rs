macro_rules! short_prefix {
    ($(left right)* left done) => {
        859u16
    };
}

macro_rules! sum {
    ($($value:expr),+ $(,)?) => {
        0u16 $(+ $value)+
    };
}

macro_rules! optional {
    ($(enabled)? done) => {
        863u16
    };
}

macro_rules! pairs {
    ($(one two)+ end) => {
        877u16
    };
}

fn main() {
    assert_eq!(short_prefix!(left done), 859);
    assert_eq!(short_prefix!(left right left done), 859);
    assert_eq!(sum!(11, 13, 17,), 41);
    assert_eq!(optional!(done), 863);
    assert_eq!(optional!(enabled done), 863);
    assert_eq!(pairs!(one two one two end), 877);
}
