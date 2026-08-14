macro_rules! expression {
    ($value:expr) => {
        $value
    };
}

fn main() {
    assert!(!expression!(if let -10i8..=-1i8 = 0i8 {
        true
    } else {
        false
    }));
}
