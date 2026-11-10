struct Parcel(u8);

macro_rules! inspect {
    ($value:expr) => { (stringify!($value), $value) };
}

macro_rules! forward {
    ($constructor:path, $argument:expr) => { inspect!($constructor($argument)) };
}

fn main() {
    let (source, parcel) = forward!(Parcel, 79);
    assert_eq!(parcel.0, 79);
    assert!(source.contains("Parcel"));
    assert!(source.contains("79"));
}
