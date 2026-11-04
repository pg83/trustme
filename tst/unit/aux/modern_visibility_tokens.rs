//@ edition: 2021

#[macro_export]
macro_rules! relative {
    ($name:ident) => { pub(in outer) fn $name() -> u16 { 131 } };
}

#[macro_export]
macro_rules! forward {
    ($visibility:vis fn $name:ident) => { $visibility fn $name() -> u16 { 137 } };
}
