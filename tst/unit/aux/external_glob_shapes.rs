pub enum Parcel { Empty, Tuple(u16), Record { value: u16 } }
pub enum Code { Low = 4, High = 9 }

#[macro_export]
macro_rules! make_parcel {
    ($value:expr) => { $crate::Parcel::Record { value: $value } };
}
