trait Measure {
    fn measured(&self) -> (i64, usize);
}

impl Measure for i32 {
    fn measured(&self) -> (i64, usize) {
        (*self as i64, std::mem::size_of::<Self>())
    }
}

impl Measure for u16 {
    fn measured(&self) -> (i64, usize) {
        (*self as i64, std::mem::size_of::<Self>())
    }
}

struct Sample<T: Measure> {
    value: T,
}

fn main() {
    let sample = Sample { value: 29 };
    let object: &dyn Measure = &sample.value;
    assert_eq!(object.measured(), (29, 4));
}
